#include "VideoReceiver.h"
#include <QDebug>
#include <QNetworkDatagram>
#include <QtEndian>
#include <QDateTime>
#include <QTimer>

VideoReceiver::VideoReceiver(quint16 port, QObject *parent)
    : QObject(parent), m_udpSocket(new QUdpSocket(this))
{
    // 强制增加 Socket 的接收缓冲区（8MB），极其关键：防止大码率视频下底层 UDP 疯狂丢包酿成花屏。
    m_udpSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);

    if (m_udpSocket->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_udpSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024); // 再次设保靠谱
        qDebug() << "VideoReceiver UDP socket bound to port" << port << "Buffer size up to 8MB";
        connect(m_udpSocket, &QUdpSocket::readyRead, this, &VideoReceiver::readPendingDatagrams);
        
        // 高频清理过期不完整的帧防止拥堵
        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &VideoReceiver::cleanOldFrames);
        timer->start(10); // 改为10ms高频检查，不给破损帧长达一秒的拥塞机会
    } else {
        qWarning() << "Failed to bind UDP port" << port;
    }
}

VideoReceiver::~VideoReceiver() {
    m_udpSocket->close();
}

void VideoReceiver::cleanOldFrames() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto it = m_frames.begin();
    while (it != m_frames.end()) {
        if (now - it.value().timestamp > 150) { 
            // 彻底去除发射“错误拼团、缺损头尾”等残次帧的魔改逻辑，
            // 所有丢包产生的问题交由后方强大的 ffmpeg av_parser 分析补救或自然跨过。
            it = m_frames.erase(it);
        } else {
            ++it;
        }
    }
}

void VideoReceiver::readPendingDatagrams() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        const QByteArray &data = datagram.data();

        if (data.size() < 8) {
            continue; // 非法包长
        }

        // 解析自定义分片协议头
        const uint16_t frameId = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()));
        const uint16_t fragIdx = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + 2));
        const uint32_t totalSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + 4));

        // 终极杀手检：防止上位机重发旧序号复用或者内存溢出（超 5MB 拒收）
        if (totalSize == 0 || totalSize > 5 * 1024 * 1024) continue;

        // 获取或创建缓存
        FrameBuffer &fb = m_frames[frameId];
        if (fb.totalSize != 0 && fb.totalSize != totalSize) {
            // 非常致命的情况出现了：FrameId 被循环复用了，而旧的分片还残留在字典里！
            // 这里正是导致 FFmpeg 报 'Two slices reporting being first in the same frame' 的幕后真凶！
            fb.fragments.clear(); 
        }

        fb.totalSize = totalSize;
        fb.fragments[fragIdx] = data.mid(8);
        fb.timestamp = QDateTime::currentMSecsSinceEpoch();

        // 计算当前帧已收集的分片大小总和
        uint32_t receivedSize = 0;
        for (auto it = fb.fragments.constBegin(); it != fb.fragments.constEnd(); ++it) {
            receivedSize += it.value().size();
        }

        // 仅当分片严格对齐相等时才进行最后组装，杜绝野指针残包组合！
        if (receivedSize >= totalSize) {
            if (receivedSize == totalSize) {
                QByteArray completeFrame(totalSize, 0);
                uint32_t offset = 0;
                bool ok = true;

                for (auto it = fb.fragments.constBegin(); it != fb.fragments.constEnd(); ++it) {
                    const int fragSize = it.value().size();
                    if (fragSize <= 0 || offset + fragSize > totalSize) {
                        ok = false;
                        break;
                    }
                    memcpy(completeFrame.data() + offset, it.value().constData(), fragSize);
                    offset += static_cast<uint32_t>(fragSize);
                }

                if (ok && offset == totalSize) {
                    emit dataReceived(completeFrame); // 发送完整纯净的包进行解码
                }
            }
            m_frames.remove(frameId); // 不管拼成功还是拼失败（比如超了），都彻底剔除掉这个毒瘤帧
        }
    }
}
