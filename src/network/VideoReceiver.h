#ifndef VIDEORECEIVER_H
#define VIDEORECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QMap>
#include <QByteArray>

class VideoReceiver : public QObject {
    Q_OBJECT
public:
    explicit VideoReceiver(quint16 port, QObject *parent = nullptr);
    ~VideoReceiver();

signals:
    void dataReceived(const QByteArray &data); // 抛出完整的组装帧给解码器

private slots:
    void readPendingDatagrams();
    void cleanOldFrames();

private:
    struct FrameBuffer {
        uint32_t totalSize = 0;
        QMap<uint16_t, QByteArray> fragments;
        qint64 timestamp = 0;
    };

    QUdpSocket *m_udpSocket;
    QMap<uint16_t, FrameBuffer> m_frames;
};

#endif // VIDEORECEIVER_H
