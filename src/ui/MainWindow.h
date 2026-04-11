#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QTimer>

class VideoReceiver;
class VideoDecoder;
class MqttManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onOfficialFrameReady(const QImage &image);
    void onCustomFrameReady(const QImage &image);
    
    // UI状态更新槽函数
    void updateHp(int hp);
    void updateHeat(int heat);
    
    // 75Hz 控制推流时钟
    void onControlTick();

private:
    VideoReceiver *m_videoReceiver = nullptr;
    VideoDecoder *m_videoDecoder = nullptr;       // 官方图传 H.265 解码器
    VideoDecoder *m_customVideoDecoder = nullptr; // 自定义图传 H.264 解码器
    MqttManager *m_mqttManager = nullptr;
    
    QImage m_currentFrame;
    bool m_useCustomVideo = false; // 按 V 切换图传源
    
    QTimer *m_controlTimer = nullptr;

    // 键鼠状态缓存
    bool m_leftButton = false;
    bool m_rightButton = false;
    bool m_midButton = false;
    int m_mouseX = 0;
    int m_mouseY = 0;
    int m_mouseZ = 0;
    uint32_t m_keyboardValue = 0;

    // 鼠标抓取状态
    bool m_mouseLocked = false;
    void setMouseLocked(bool locked);
};

#endif // MAINWINDOW_H
