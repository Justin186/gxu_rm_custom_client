#include "MainWindow.h"
#include <QPainter>
#include <QDebug>
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
    // 这里指向你说的固定服务器IP：192.168.12.1 或者是本地测试时的 127.0.0.1 端口 3333
    // 你可以随时根据 protocol.md 更改
    m_mqttManager->connectToBroker("127.0.0.1", 3333);

    // 绑定 UDP 数据流到解码线程
    connect(m_videoReceiver, &VideoReceiver::dataReceived, m_videoDecoder, &VideoDecoder::pushData);
    
    // 绑定解码线程输出到主线程渲染
    connect(m_videoDecoder, &VideoDecoder::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    // 绑定数据模型变化到UI更新
    connect(&RobotState::instance(), &RobotState::hpChanged, this, &MainWindow::updateHp);
    connect(&RobotState::instance(), &RobotState::heatChanged, this, &MainWindow::updateHeat);

    // 启动解码线程并赋予高线程优先级，保障其吞吐能力
    m_videoDecoder->start(QThread::HighPriority);
}

MainWindow::~MainWindow()
{
    m_videoDecoder->stop();
    m_videoDecoder->wait();
}

void MainWindow::onFrameReady(const QImage &image) {
    m_currentFrame = image;
    // 触发重绘
    update();
}

void MainWindow::updateHp(int hp) {
    qDebug() << "UI HP updated:" << hp;
    update(); // 更新显示血量
}

void MainWindow::updateHeat(int heat) {
    qDebug() << "UI Heat updated:" << heat;
    update();
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

    // 绘制简易 HUD 层
    painter.setPen(Qt::green);
    painter.setFont(QFont("Arial", 16, QFont::Bold));
    
    QString hpText = QString("HP: %1 / %2").arg(RobotState::instance().hp()).arg(RobotState::instance().maxHp());
    QString heatText = QString("Heat: %1").arg(RobotState::instance().heat());

    painter.drawText(20, 40, hpText);
    painter.drawText(20, 70, heatText);
    
    // 自定义准星
    int cx = width() / 2;
    int cy = height() / 2;
    painter.setPen(QPen(Qt::red, 2));
    painter.drawLine(cx - 20, cy, cx + 20, cy);
    painter.drawLine(cx, cy - 20, cx, cy + 20);
    painter.drawEllipse(QPoint(cx, cy), 15, 15);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    rm_client_up::RemoteControl cmd;
    cmd.set_left_button_down(event->button() == Qt::LeftButton);
    cmd.set_right_button_down(event->button() == Qt::RightButton);
    
    QByteArray payload;
    payload.resize(cmd.ByteSizeLong());
    cmd.SerializeToArray(payload.data(), payload.size());
    
    m_mqttManager->publishMsg("KeyboardMouseControl", payload);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    Q_UNUSED(event);
    rm_client_up::RemoteControl cmd;
    cmd.set_left_button_down(false);
    cmd.set_right_button_down(false);
    
    QByteArray payload;
    payload.resize(cmd.ByteSizeLong());
    cmd.SerializeToArray(payload.data(), payload.size());
    m_mqttManager->publishMsg("KeyboardMouseControl", payload);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // 示例：按 Q 键发出特殊点击或数据
    if (event->key() == Qt::Key_Q) {
        rm_client_up::MapClickInfoNotify mapCmd;
        mapCmd.set_is_send_all(1);
        mapCmd.set_mode(1);
        mapCmd.set_type(1);
        QByteArray payload;
        payload.resize(mapCmd.ByteSizeLong());
        mapCmd.SerializeToArray(payload.data(), payload.size());
        m_mqttManager->publishMsg("MapClickInfoNotify", payload);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    Q_UNUSED(event);
}
