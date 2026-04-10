#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QMouseEvent>
#include <QKeyEvent>

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
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void onFrameReady(const QImage &image);
    // UI状态更新槽函数
    void updateHp(int hp);
    void updateHeat(int heat);

private:
    VideoReceiver *m_videoReceiver = nullptr;
    VideoDecoder *m_videoDecoder = nullptr;
    MqttManager *m_mqttManager = nullptr;
    
    QImage m_currentFrame;
};

#endif // MAINWINDOW_H
