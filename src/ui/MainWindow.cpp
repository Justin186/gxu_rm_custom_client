#include "MainWindow.h"
#include <QPainter>
#include <QLinearGradient>
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
    m_mqttManager->connectToBroker("192.168.12.1", 3333);

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

        // 如果是自定义图传，绘制 6x6 网格线 (30%透明度)
        if (m_useCustomVideo) {
            painter.setPen(QPen(QColor(255, 255, 255, 77), 1, Qt::SolidLine));
            float stepX = scaled.width() / 6.0f;
            float stepY = scaled.height() / 6.0f;
            for (int i = 1; i < 6; ++i) {
                painter.drawLine(x, y + static_cast<int>(i * stepY), x + scaled.width(), y + static_cast<int>(i * stepY));
                painter.drawLine(x + static_cast<int>(i * stepX), y, x + static_cast<int>(i * stepX), y + scaled.height());
            }
        }
    } else {
        painter.setPen(QColor(0, 255, 255, 150));
        painter.setFont(QFont("Consolas", 14));
        QString waitText = m_useCustomVideo ? "WAITING FOR CUSTOM BYTEBLOCK (H.264) ..." : "WAITING FOR OFFICIAL STREAM (UDP: 3334 HEVC) ...";
        painter.drawText(rect(), Qt::AlignCenter, waitText);
    }
    
    // 绘制图传模式提示
    painter.setPen(QColor(0, 255, 0));
    painter.setFont(QFont("Consolas", 10, QFont::Bold));
    QString modeText = m_useCustomVideo ? "CURRENT: CUSTOM" : "CURRENT: OFFICIAL";
    painter.drawText(10, 20, modeText);

    // 绘制图传诊断信息与热键 (排查丢包花屏)
    VideoDecoder *activeDecoder = m_useCustomVideo ? m_customVideoDecoder : m_videoDecoder;
    if (activeDecoder) {
        int pkts = activeDecoder->getReceivedPackets();
        int frames = activeDecoder->getDecodedFrames();
        int errors = activeDecoder->getDecodeErrors();
        int queueSize = activeDecoder->getQueueSize();
        
        painter.setPen(QColor(255, 255, 0)); // 黄色警告字
        painter.setFont(QFont("Consolas", 10, QFont::Bold));
        QString debugText = QString("Packets: %1 | Decoded: %2 | Errors: %3")
                              .arg(pkts).arg(frames).arg(errors);
        painter.drawText(10, 40, debugText);
    }

        // ==========================================
        // 2. 顶部赛事计分板与血量条布局（分秒格式时间，左红右蓝）
        // ==========================================
        const int topY = 8;
        const int barHOutpost = 16;
        const int barHBase = 20;
        const int slant = 18;
        const int spacer = 10;
        const int redOutpostW = 116;
        const int redBaseW = 240;
        const int centerClusterW = 220;
        const int blueBaseW = 240;
        const int blueOutpostW = 116;

        const int topCx = width() / 2;
        const int totalW = redOutpostW + spacer + redBaseW + spacer + centerClusterW + spacer + blueBaseW + spacer + blueOutpostW;
        int x0 = topCx - totalW / 2;

        auto drawParallelogramBar = [&](int x, int y, int w, int h, int slantOffset,
                                        float ratio, const QColor &backLeft, const QColor &backRight,
                                        const QColor &fillLeft, const QColor &fillMid, const QColor &fillRight,
                                        const QColor &textColor, const QString &label, bool alignLeft = false) {
            auto makePoly = [&](int innerW) {
                int safeW = std::max(0, innerW);
                return QPolygon{
                    QPoint(x, y),
                    QPoint(x + safeW, y),
                    QPoint(x + safeW + slantOffset, y + h),
                    QPoint(x + slantOffset, y + h)
                };
            };

            QPolygon backPoly = makePoly(w);
            painter.setPen(QPen(backRight, 1));
            painter.setBrush(backLeft);
            painter.drawPolygon(backPoly);

            int fillW = std::max(2, static_cast<int>(w * qBound(0.0f, ratio, 1.0f)));
            QPolygon fillPoly = makePoly(fillW);
            QLinearGradient grad(QPointF(x, y), QPointF(x, y + h));
            grad.setColorAt(0.0, fillRight);
            grad.setColorAt(0.65, fillMid);
            grad.setColorAt(1.0, fillLeft);
            painter.setBrush(grad);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(fillPoly);

            painter.setPen(textColor);
            painter.setFont(QFont("Consolas", 10, QFont::Bold));
            QRect textRect(x + std::min(0, slantOffset), y - 1, w + std::abs(slantOffset), h + 2);
            painter.drawText(textRect, alignLeft ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter, label);
        };

        // 红方前哨（短）
        int redOutpostHp = RobotState::instance().redOutpostHp();
        int redOutpostMax = 1500;
        float roRatio = qBound(0.0f, (float)redOutpostHp / redOutpostMax, 1.0f);
        drawParallelogramBar(
            x0, topY + 2, redOutpostW, barHOutpost, slant, roRatio,
            QColor(35, 10, 10, 175), QColor(180, 50, 50, 220),
            QColor(120, 20, 20), QColor(255, 90, 90), QColor(255, 170, 110),
            Qt::white, QString("%1/%2").arg(redOutpostHp).arg(redOutpostMax)
        );

        // 红方基地（长）
        int redBaseHp = RobotState::instance().redBaseHp();
        int redBaseMax = 5000;
        float rbRatio = qBound(0.0f, (float)redBaseHp / redBaseMax, 1.0f);
        drawParallelogramBar(
            x0 + redOutpostW + spacer, topY, redBaseW, barHBase, slant, rbRatio,
            QColor(35, 10, 10, 175), QColor(180, 50, 50, 220),
            QColor(120, 20, 20), QColor(255, 90, 90), QColor(255, 170, 110),
            Qt::white, QString("%1/%2").arg(redBaseHp).arg(redBaseMax)
        );

        // 中间区：红方分数、倒计时、蓝方分数
        int redScore = RobotState::instance().redScore();
        int blueScore = RobotState::instance().blueScore();
        int secs = RobotState::instance().stageCountdown();
        int mm = secs / 60;
        int ss = secs % 60;
        QString timeText = QString("%1:%2").arg(mm).arg(ss, 2, 10, QChar('0'));

        int centerX = x0 + redOutpostW + spacer + redBaseW + spacer;
        QRect centerRect(centerX, topY - 2, centerClusterW, 32);
        QPolygon centerPoly;
        centerPoly << QPoint(centerRect.left(), centerRect.top())
               << QPoint(centerRect.right(), centerRect.top())
               << QPoint(centerRect.right() - 30, centerRect.bottom())
               << QPoint(centerRect.left() + 30, centerRect.bottom());
        painter.setBrush(QColor(10, 18, 28, 175));
        painter.setPen(QPen(QColor(0, 255, 255, 90), 1));
        painter.drawPolygon(centerPoly);

        painter.setFont(QFont("Impact", 22, QFont::Bold));
        painter.setPen(QColor(255, 120, 120));
        painter.drawText(topCx - 80, topY + 24, QString::number(redScore));

        painter.setFont(QFont("Consolas", 18, QFont::Bold));
        painter.setPen(Qt::white);
        painter.drawText(topCx - 30, topY + 22, timeText);

        painter.setFont(QFont("Impact", 22, QFont::Bold));
        painter.setPen(QColor(130, 180, 255));
        painter.drawText(topCx + 60, topY + 24, QString::number(blueScore));

        // 蓝方基地与前哨（基地长，前哨短）
        int blueBaseHp = RobotState::instance().blueBaseHp();
        int blueBaseMax = 5000;
        float bbRatio = qBound(0.0f, (float)blueBaseHp / blueBaseMax, 1.0f);
        drawParallelogramBar(
            centerX + centerClusterW + spacer, topY, blueBaseW, barHBase, -slant, bbRatio,
            QColor(10, 10, 35, 175), QColor(50, 90, 180, 220),
            QColor(20, 30, 120), QColor(90, 150, 255), QColor(160, 220, 255),
            Qt::white, QString("%1/%2").arg(blueBaseHp).arg(blueBaseMax)
        );

        int blueOutpostHp = RobotState::instance().blueOutpostHp();
        int blueOutpostMax = 1500;
        float boRatio = qBound(0.0f, (float)blueOutpostHp / blueOutpostMax, 1.0f);
        drawParallelogramBar(
            centerX + centerClusterW + spacer + blueBaseW + spacer, topY + 2, blueOutpostW, barHOutpost, -slant, boRatio,
            QColor(10, 10, 35, 175), QColor(50, 90, 180, 220),
            QColor(20, 30, 120), QColor(90, 150, 255), QColor(160, 220, 255),
            Qt::white, QString("%1/%2").arg(blueOutpostHp).arg(blueOutpostMax)
        );

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

    auto drawSlantedFillBar = [&](int x, int y, int w, int h, int slantOffset, float ratio,
                                  const QColor &bgLeft, const QColor &bgRight,
                                  const QColor &fgLeft, const QColor &fgMid, const QColor &fgRight,
                                  const QString &valueText) {
        auto makePoly = [&](int innerW) {
            int safeW = std::max(0, innerW);
            return QPolygon{
                QPoint(x, y),
                QPoint(x + safeW, y),
                QPoint(x + safeW + slantOffset, y + h),
                QPoint(x + slantOffset, y + h)
            };
        };

        painter.setPen(QPen(bgRight, 1));
        painter.setBrush(bgLeft);
        painter.drawPolygon(makePoly(w));

        int fillW = std::max(2, static_cast<int>(w * qBound(0.0f, ratio, 1.0f)));
        QLinearGradient grad(QPointF(x, y), QPointF(x, y + h));
        grad.setColorAt(0.0, fgLeft);
        grad.setColorAt(0.65, fgMid);
        grad.setColorAt(1.0, fgRight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPolygon(makePoly(fillW));

        painter.setPen(Qt::white);
        painter.setFont(QFont("Consolas", 12, QFont::Bold));
        painter.drawText(QRect(x + std::min(0, slantOffset), y - 1, w + std::abs(slantOffset), h + 2), Qt::AlignCenter, valueText);
    };

    // HP 条（斜的平行四边形 + 渐变）
    drawSlantedFillBar(
        40, height() - 120, 220, 18, 7, hpRatio,
        QColor(35, 35, 35, 165), QColor(65, 65, 65, 165),
        QColor(0, 210, 90), QColor(90, 255, 110), QColor(255, 235, 70),
        QString("%1/%2").arg(hp).arg(maxHp)
    );

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

    // 热量条（斜的平行四边形 + 渐变）
    drawSlantedFillBar(
        width() - 250, height() - 120, 200, 18, -7, heatRatio,
        QColor(35, 20, 0, 165), QColor(65, 35, 0, 165),
        QColor(255, 150, 0), QColor(255, 220, 0), QColor(255, 60, 60),
        QString("%1/%2").arg(heat).arg(maxHeat)
    );

    // 子弹射速
    painter.setPen(QColor(255, 220, 120));
    painter.drawText(width() - 260, height() - 70, QString("FIRE RATE: %1").arg(RobotState::instance().fireRate(), 0, 'f', 1));

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
        painter.drawText(topCx - 82, height() - 45, "[ MOUSE UNLOCKED ]");
    } else {
        painter.setPen(QColor(0, 255, 100, 220));
        painter.drawText(topCx - 70, height() - 45, "[ MOUSE LOCKED ]");
    }
    
    painter.setPen(QColor(0, 200, 255, 150));
    painter.drawText(topCx - 190, height() - 20, "[H] EXH  [O/I] AMMO  [M] MAP  [TAB] STATS");
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
        } else if (key == Qt::Key_R) { // 按 R 手动矫正/清空解码器缓存
            if (m_useCustomVideo && m_customVideoDecoder) m_customVideoDecoder->requestFlush();
            else if (!m_useCustomVideo && m_videoDecoder) m_videoDecoder->requestFlush();
            qDebug() << "🔄 请求强制刷新图传底层缓存，尝试恢复花屏...";
            m_keyboardValue |= (1 << 8);
        } else if (key == Qt::Key_W) m_keyboardValue |= (1 << 0);
        else if (key == Qt::Key_S) m_keyboardValue |= (1 << 1);
        else if (key == Qt::Key_A) m_keyboardValue |= (1 << 2);
        else if (key == Qt::Key_D) m_keyboardValue |= (1 << 3);
        else if (key == Qt::Key_Shift) m_keyboardValue |= (1 << 4);
        else if (key == Qt::Key_Control) m_keyboardValue |= (1 << 5);
        else if (key == Qt::Key_Q) m_keyboardValue |= (1 << 6);
        else if (key == Qt::Key_E) m_keyboardValue |= (1 << 7);
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
