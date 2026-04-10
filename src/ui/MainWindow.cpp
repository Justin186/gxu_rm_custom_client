#include "MainWindow.h"
#include <QPainter>
#include <QDebug>
#include <QApplication>
#include "../network/VideoReceiver.h"
#include "../video/VideoDecoder.h"
#include "../network/MqttManager.h"
#include "../model/RobotState.h"
#include "messages.pb.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Default window size for a modern UI (eg. 720p or 1080p stream) 
    resize(1280, 720);
    setWindowTitle("RoboMaster Custom Client");

    // 初始化网络与解码层
    m_videoReceiver = new VideoReceiver(3334, this);
    m_videoDecoder = new VideoDecoder(this);
    
    m_mqttManager = new MqttManager("CustomClient_UI", this);
    m_mqttManager->connectToBroker("127.0.0.1", 3333);

    // 绑定 UDP 数据流到解码线程
    connect(m_videoReceiver, &VideoReceiver::dataReceived, m_videoDecoder, &VideoDecoder::pushData);
    
    // 绑定解码线程输出到主线程渲染
    connect(m_videoDecoder, &VideoDecoder::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    // 绑定数据模型变化到UI更新
    connect(&RobotState::instance(), &RobotState::stateUpdated, this, [this](){ update(); });
    

    // 启动解码线程并赋予高线程优先级，保障其吞吐能力
    m_videoDecoder->start(QThread::HighPriority);

    // 开启鼠标滑动追踪
    setMouseTracking(true);
    QWidget::setMouseTracking(true);

    // 启动75Hz定时器 (约13ms)，满足实战要求的定频发包
    m_controlTimer = new QTimer(this);
    connect(m_controlTimer, &QTimer::timeout, this, &MainWindow::onControlTick);
    m_controlTimer->start(13);
}

MainWindow::~MainWindow()
{
    m_videoDecoder->stop();
    m_videoDecoder->wait();
    if(m_mouseLocked) {
        setMouseLocked(false);
    }
}

void MainWindow::onFrameReady(const QImage &image) {
    m_currentFrame = image;
    update();
}

void MainWindow::updateHp(int hp) {
    update(); // 更新显示血量
}

void MainWindow::updateHeat(int heat) {
    update();
}

void MainWindow::setMouseLocked(bool locked) {
    m_mouseLocked = locked;
    if (locked) {
        setCursor(Qt::BlankCursor); // 隐藏鼠标指针
        // 首先把鼠标中心强制拉回界面中心
        QCursor::setPos(mapToGlobal(rect().center()));
        qDebug() << "Mouse Locked! Press ESC to unlock.";
    } else {
        setCursor(Qt::ArrowCursor); // 恢复鼠标指针
        qDebug() << "Mouse Unlocked.";
    }
}

void MainWindow::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black); // 黑色底
    
    // 绘制视频背景保持缩放比
    if (!m_currentFrame.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(m_currentFrame);
        QPixmap scaled = pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    } else {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "Waiting for Video Stream (UDP 3334)...");
    }

    // 绘制简易 HUD 层 (血条等)
    painter.setPen(Qt::green);
    painter.setFont(QFont("Arial", 16, QFont::Bold));
    
    QString hpText = QString("HP: %1 / %2").arg(RobotState::instance().hp()).arg(RobotState::instance().maxHp());
    QString heatText = QString("Heat: %1 / %2").arg(RobotState::instance().heat()).arg(RobotState::instance().maxHeat());
    QString ammoText = QString("Ammo: %1").arg(RobotState::instance().remainingAmmo());
    QString scoreText = QString("RED %1 : %2 BLUE").arg(RobotState::instance().redScore()).arg(RobotState::instance().blueScore());
    QString timeText = QString("Time Left: %1s").arg(RobotState::instance().stageCountdown());

    // 左侧状态栏
    painter.drawText(20, 60, hpText);
    painter.drawText(20, 90, heatText);
    painter.drawText(20, 120, ammoText);

    // 顶部中间计分板
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 20, QFont::Bold));
    QFontMetrics fm(painter.font());
    painter.drawText((width() - fm.horizontalAdvance(scoreText)) / 2, 40, scoreText);
    painter.drawText((width() - fm.horizontalAdvance(timeText)) / 2, 70, timeText);
    
    // 操作提示区域
    painter.setPen(Qt::yellow);
    painter.setFont(QFont("Arial", 12, QFont::Normal));
    if (!m_mouseLocked) {
        painter.drawText(20, height() - 40, "[Click Screen to Lock Mouse (Press ESC to Unlock)]");
    } else {
        painter.drawText(20, height() - 40, "[Mouse Locked - Active Mode]");
    }
    
    painter.setPen(Qt::cyan);
    painter.drawText(20, height() - 20, "[H] Remote Exchange  [O/I] Ammo Supply  [M] Map  [TAB] Stats");

    // 自定义准星
    int cx = width() / 2;
    int cy = height() / 2;
    painter.setPen(QPen(Qt::red, 2));
    painter.drawLine(cx - 20, cy, cx + 20, cy);
    painter.drawLine(cx, cy - 20, cx, cy + 20);
    painter.drawEllipse(QPoint(cx, cy), 15, 15);
}

void MainWindow::focusOutEvent(QFocusEvent *event) {
    if (m_mouseLocked) {
        setMouseLocked(false);
    }
    QMainWindow::focusOutEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (!m_mouseLocked) {
        setMouseLocked(true);
        return;
    }
    if (event->button() == Qt::LeftButton) m_leftButton = true;
    if (event->button() == Qt::RightButton) m_rightButton = true;
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_leftButton = false;
    if (event->button() == Qt::RightButton) m_rightButton = false;
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_mouseLocked) return;

    QPoint center = mapToGlobal(rect().center());
    QPoint current = QCursor::pos();
    
    int dx = current.x() - center.x();
    int dy = current.y() - center.y();

    // 累加位移给接下来的发送时针读取
    if (dx != 0 || dy != 0) {
        m_mouseX += dx;
        m_mouseY += dy;
        // 把鼠标立即挪回中心点锁定
        QCursor::setPos(center);
    }
}

void MainWindow::wheelEvent(QWheelEvent *event) {
    if (!m_mouseLocked) return;
    int delta = event->angleDelta().y();
    if (delta != 0) {
        // wheel delta is usually +/- 120 per notch
        m_mouseZ += (delta > 0) ? -1 : 1; 
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat()) {
        int key = event->key();
        if (key == Qt::Key_Escape) {
            setMouseLocked(false);
        } else if (key == Qt::Key_W) m_keyboardValue |= (1 << 0);
        else if (key == Qt::Key_S) m_keyboardValue |= (1 << 1);
        else if (key == Qt::Key_A) m_keyboardValue |= (1 << 2);
        else if (key == Qt::Key_D) m_keyboardValue |= (1 << 3);
        else if (key == Qt::Key_Shift) m_keyboardValue |= (1 << 4);
        else if (key == Qt::Key_Control) m_keyboardValue |= (1 << 5);
        else if (key == Qt::Key_Q) m_keyboardValue |= (1 << 6);
        else if (key == Qt::Key_E) m_keyboardValue |= (1 << 7);
        else if (key == Qt::Key_R) m_keyboardValue |= (1 << 8);
        else if (key == Qt::Key_F) m_keyboardValue |= (1 << 9);
        else if (key == Qt::Key_G) m_keyboardValue |= (1 << 10);
        else if (key == Qt::Key_Z) m_keyboardValue |= (1 << 11);
        else if (key == Qt::Key_X) m_keyboardValue |= (1 << 12);
        else if (key == Qt::Key_C) m_keyboardValue |= (1 << 13);
        else if (key == Qt::Key_V) m_keyboardValue |= (1 << 14);
        else if (key == Qt::Key_B) m_keyboardValue |= (1 << 15);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat()) {
        int key = event->key();
        if (key == Qt::Key_W) m_keyboardValue &= ~(1 << 0);
        else if (key == Qt::Key_S) m_keyboardValue &= ~(1 << 1);
        else if (key == Qt::Key_A) m_keyboardValue &= ~(1 << 2);
        else if (key == Qt::Key_D) m_keyboardValue &= ~(1 << 3);
        else if (key == Qt::Key_Shift) m_keyboardValue &= ~(1 << 4);
        else if (key == Qt::Key_Control) m_keyboardValue &= ~(1 << 5);
        else if (key == Qt::Key_Q) m_keyboardValue &= ~(1 << 6);
        else if (key == Qt::Key_E) m_keyboardValue &= ~(1 << 7);
        else if (key == Qt::Key_R) m_keyboardValue &= ~(1 << 8);
        else if (key == Qt::Key_F) m_keyboardValue &= ~(1 << 9);
        else if (key == Qt::Key_G) m_keyboardValue &= ~(1 << 10);
        else if (key == Qt::Key_Z) m_keyboardValue &= ~(1 << 11);
        else if (key == Qt::Key_X) m_keyboardValue &= ~(1 << 12);
        else if (key == Qt::Key_C) m_keyboardValue &= ~(1 << 13);
        else if (key == Qt::Key_V) m_keyboardValue &= ~(1 << 14);
        else if (key == Qt::Key_B) m_keyboardValue &= ~(1 << 15);
    }
}

// 75Hz 定时器将全量外设状态发往后端 MQTT 与下位机
void MainWindow::onControlTick() {
    bool isZero = (m_mouseX == 0 && m_mouseY == 0 && m_mouseZ == 0 && 
                   !m_leftButton && !m_rightButton && !m_midButton && 
                   m_keyboardValue == 0);

    // Filter spam: 发送频率控制 (防止服务器空跑刷屏报错)
    static int emptyTicks = 0;
    if (isZero) {
        emptyTicks++;
        // 允许连续发送 2 次空包用来向服务端确认松手，之后变为 1Hz 心跳以防止掉线
        if (emptyTicks > 2 && emptyTicks < 75) {
            return;
        }
        if (emptyTicks >= 75) {
            emptyTicks = 2; // 继续发一次心跳包
        }
    } else {
        emptyTicks = 0; // 重置计数
    }

    rm_client_up::RemoteControl cmd;
    
    // 写入鼠标状态（注意云台视角 x 和 y 是基于屏幕差值的微调量）
    cmd.set_mouse_x(m_mouseX);
    cmd.set_mouse_y(m_mouseY);
    cmd.set_mouse_z(m_mouseZ);
    cmd.set_left_button_down(m_leftButton);
    cmd.set_right_button_down(m_rightButton);
    cmd.set_mid_button_down(m_midButton);
    
    // 写入键盘掩码
    cmd.set_keyboard_value(m_keyboardValue);

    // 将内容打包 Protobuf 序列化为指定格式
    QByteArray payload;
    payload.resize(cmd.ByteSizeLong());
    cmd.SerializeToArray(payload.data(), payload.size());
    
    m_mqttManager->publishMsg("KeyboardMouseControl", payload);

    // 发送之后立即清空当轮循环的增量值，按键的电平掩码不需要清空
    m_mouseX = 0;
    m_mouseY = 0;
    m_mouseZ = 0;
}
