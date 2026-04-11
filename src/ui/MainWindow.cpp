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
    m_videoDecoder = new VideoDecoder(AV_CODEC_ID_HEVC, this);        // 官方 UDP HEVC 流
    m_customVideoDecoder = new VideoDecoder(AV_CODEC_ID_H264, this);  // 自定义 MQTT H264 流
    
    m_mqttManager = new MqttManager("101", this);
    m_mqttManager->connectToBroker("192.168.1.30", 3333);

    // 绑定 UDP 数据流到解码线程
    connect(m_videoReceiver, &VideoReceiver::dataReceived, m_videoDecoder, &VideoDecoder::pushData);
    
    // 绑定 MQTT 0x0310 自定义数据流到 自定义图传解码线程
    connect(&RobotState::instance(), &RobotState::customVideoReceived, m_customVideoDecoder, &VideoDecoder::pushData, Qt::QueuedConnection);
    
    // 绑定解码线程输出到主线程渲染
    connect(m_videoDecoder, &VideoDecoder::frameReady, this, &MainWindow::onOfficialFrameReady, Qt::QueuedConnection);
    connect(m_customVideoDecoder, &VideoDecoder::frameReady, this, &MainWindow::onCustomFrameReady, Qt::QueuedConnection);

    // 绑定数据模型变化到UI更新
    connect(&RobotState::instance(), &RobotState::stateUpdated, this, [this](){ update(); });
    

    // 启动解码线程并赋予高线程优先级，保障其吞吐能力
    m_videoDecoder->start(QThread::HighPriority);
    m_customVideoDecoder->start(QThread::HighPriority);

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
    m_customVideoDecoder->stop();
    m_customVideoDecoder->wait();
    if(m_mouseLocked) {
        setMouseLocked(false);
    }
}

void MainWindow::onOfficialFrameReady(const QImage &image) {
    if (!m_useCustomVideo) {
        m_currentFrame = image;
        update();
    }
}

void MainWindow::onCustomFrameReady(const QImage &image) {
    if (m_useCustomVideo) {
        m_currentFrame = image;
        update();
    }
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
    painter.setRenderHint(QPainter::Antialiasing); // 开启抗锯齿让线条平滑
    painter.fillRect(rect(), Qt::black); 
    
    // ==========================================
    // 1. 绘制等比例实战视频背景
    // ==========================================
    if (!m_currentFrame.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(m_currentFrame);
        QPixmap scaled = pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    } else {
        painter.setPen(QColor(0, 255, 255, 150));
        painter.setFont(QFont("Consolas", 14));
        QString waitText = m_useCustomVideo ? "WAITING FOR CUSTOM BYTEBLOCK (H.264) ..." : "WAITING FOR OFFICIAL STREAM (UDP: 3334 HEVC) ...";
        painter.drawText(rect(), Qt::AlignCenter, waitText);
    }
    
    // 绘制图传模式提示
    painter.setPen(QColor(0, 255, 0));
    painter.setFont(QFont("Consolas", 14, QFont::Bold));
    QString modeText = m_useCustomVideo ? "[V: Switch to Official] CURRENT: CUSTOM H.264 STREAM" : "[V: Switch to Custom] CURRENT: OFFICIAL HEVC STREAM";
    painter.drawText(10, 30, modeText);

    // ==========================================
    // 2. 绘制顶部赛事计分板 (科幻多边形)
    // ==========================================
    QPolygon topBar;
    int topCx = width() / 2;
    topBar << QPoint(topCx - 260, 0) << QPoint(topCx + 260, 0)
           << QPoint(topCx + 220, 60) << QPoint(topCx - 220, 60);

    // 半透明科幻底层
    painter.setBrush(QColor(10, 20, 30, 180));
    painter.setPen(QPen(QColor(0, 255, 255, 120), 2));
    painter.drawPolygon(topBar);

    painter.setFont(QFont("Impact", 22, QFont::Bold));
    painter.setPen(QColor(255, 80, 80)); // 红色方
    painter.drawText(topCx - 180, 42, QString("RED %1").arg(RobotState::instance().redScore()));
    
    painter.setPen(QColor(80, 150, 255)); // 蓝色方
    painter.drawText(topCx + 80, 42, QString("BLUE %1").arg(RobotState::instance().blueScore()));
    
    painter.setPen(Qt::white);
    painter.setFont(QFont("Consolas", 14, QFont::Bold));
    QString timeText = QString("TIME: %1 S").arg(RobotState::instance().stageCountdown());
    painter.drawText(topCx - 45, 40, timeText);

    // ==========================================
    // 3. 绘制左下角生命槽与弹药 (装甲风格)
    // ==========================================
    int hp = RobotState::instance().hp();
    int maxHp = RobotState::instance().maxHp();
    if (maxHp <= 0) maxHp = 1;
    float hpRatio = std::min(1.0f, std::max(0.0f, (float)hp / maxHp));

    // 背景底板
    QPolygon leftPanel;
    leftPanel << QPoint(20, height() - 160) << QPoint(280, height() - 160)
              << QPoint(320, height() - 40) << QPoint(20, height() - 40);
    painter.setBrush(QColor(0, 30, 10, 160));
    painter.setPen(QPen(QColor(0, 255, 150, 100), 2));
    painter.drawPolygon(leftPanel);

    // HP 文本
    painter.setFont(QFont("Consolas", 12, QFont::Bold));
    painter.setPen(QColor(0, 255, 150));
    painter.drawText(40, height() - 130, "ARMOR HP");

    // HP 进度条底槽
    painter.setBrush(QColor(50, 50, 50, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRect(40, height() - 120, 220, 15);
    
    // HP 渐变变色
    QColor hpColor = hpRatio > 0.3 ? QColor(0, 255, 100) : QColor(255, 50, 50);
    painter.setBrush(hpColor);
    painter.drawRect(40, height() - 120, 220 * hpRatio, 15);
    
    painter.setPen(Qt::white);
    painter.drawText(40 + 220 * hpRatio + 5, height() - 108, QString("%1/%2").arg(hp).arg(maxHp));

    // 弹药数值
    painter.setPen(QColor(0, 255, 255));
    painter.drawText(40, height() - 70, QString("AMMO: %1").arg(RobotState::instance().remainingAmmo()));

    // ==========================================
    // 4. 绘制右下角枪管热量 (警示风格)
    // ==========================================
    int heat = RobotState::instance().heat();
    int maxHeat = RobotState::instance().maxHeat();
    if (maxHeat <= 0) maxHeat = 1;
    float heatRatio = std::min(1.0f, std::max(0.0f, (float)heat / maxHeat));

    QPolygon rightPanel;
    rightPanel << QPoint(width() - 280, height() - 160) << QPoint(width() - 20, height() - 160)
               << QPoint(width() - 20, height() - 40) << QPoint(width() - 320, height() - 40);
    painter.setBrush(QColor(40, 10, 0, 160));
    painter.setPen(QPen(QColor(255, 100, 0, 100), 2));
    painter.drawPolygon(rightPanel);

    painter.setPen(QColor(255, 150, 0));
    painter.drawText(width() - 250, height() - 130, "GUN HEAT");

    // 热量底槽
    painter.setBrush(QColor(50, 50, 50, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRect(width() - 250, height() - 120, 200, 15);
    
    QColor heatColor = heatRatio < 0.8 ? QColor(255, 150, 0) : QColor(255, 50, 50);
    painter.setBrush(heatColor);
    painter.drawRect(width() - 250, height() - 120, 200 * heatRatio, 15);
    
    painter.setPen(Qt::white);
    painter.drawText(width() - 250 - 45, height() - 108, QString("%1/%2").arg(heat).arg(maxHeat));

    // ==========================================
    // 5. 准星与弹道线 (狙击手/步兵专属)
    // ==========================================
    int cx = width() / 2;
    int cy = height() / 2;
    
    // 中心原点
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 50, 50, 220));
    painter.drawEllipse(QPoint(cx, cy), 2, 2);
    
    // 高科技分段圆环
    painter.setPen(QPen(QColor(0, 255, 200, 180), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(cx - 25, cy - 25, 50, 50, 30 * 16, 120 * 16);
    painter.drawArc(cx - 25, cy - 25, 50, 50, 210 * 16, 120 * 16);
    
    // T字准星角
    painter.drawLine(cx - 40, cy, cx - 15, cy);
    painter.drawLine(cx + 15, cy, cx + 40, cy);
    painter.drawLine(cx, cy - 40, cx, cy - 15);
    painter.drawLine(cx, cy + 15, cx, cy + 40);
    
    // 虚拟抛物预测线 (下坠刻度估算)
    painter.setPen(QPen(QColor(0, 255, 200, 100), 1, Qt::DotLine));
    painter.drawLine(cx, cy + 45, cx, cy + 120);
    painter.drawLine(cx - 10, cy + 80, cx + 10, cy + 80);
    painter.drawLine(cx - 15, cy + 120, cx + 15, cy + 120);

    // ==========================================
    // 6. 操作提示区域 (底部居中)
    // ==========================================
    painter.setFont(QFont("Consolas", 12, QFont::Normal));
    if (!m_mouseLocked) {
        painter.setPen(QColor(255, 255, 0, 220));
        painter.drawText(cx - 230, height() - 45, "[ CLICK SCREEN TO LOCK MOUSE (PRESS ESC TO UNLOCK) ]");
    } else {
        painter.setPen(QColor(0, 255, 100, 220));
        painter.drawText(cx - 160, height() - 45, "[ MOUSE LOCKED - COMBAT MODE ACTIVE ]");
    }
    
    painter.setPen(QColor(0, 200, 255, 150));
    painter.drawText(cx - 240, height() - 20, "[H] EXH  [O/I] AMMO  [M] MAP  [TAB] STATS");
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
        } else if (key == Qt::Key_V) {
            m_useCustomVideo = !m_useCustomVideo;
            qDebug() << "📸 图传切换至:" << (m_useCustomVideo ? "Custom ByteBlock H.264" : "Official UDP HEVC");
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
