#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <list>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

class VideoDecoder : public QThread {
    Q_OBJECT
public:
    explicit VideoDecoder(AVCodecID codecId = AV_CODEC_ID_HEVC, QObject *parent = nullptr);
    ~VideoDecoder();

    // 接收子线程安全的调用
    void pushData(const QByteArray &data);
    void stop();
    void requestFlush(); // 请求手工刷新解码器缓冲清理花屏

    int getReceivedPackets() const { return m_receivedPackets.load(); }
    int getDecodedFrames() const { return m_decodedFrames.load(); }
    int getDecodeErrors() const { return m_decodeErrors.load(); }
    int getQueueSize() { QMutexLocker locker(&m_mutex); return m_queue.size(); }

signals:
    void frameReady(const QImage &image);

protected:
    void run() override;

private:
    void processData(const uint8_t *data, int size);

    // FFmpeg structures
    const AVCodec *m_codec = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    AVCodecParserContext *m_parser = nullptr;
    AVFrame *m_frame = nullptr;
    AVFrame *m_rgbFrame = nullptr;
    AVPacket *m_packet = nullptr;
    SwsContext *m_swsCtx = nullptr;

    int m_width = 0;
    int m_height = 0;
    int m_pixFmt = -1; // 保存 AVPixelFormat

    bool m_running = true;
    std::atomic<bool> m_needFlush{false};
    std::atomic<int> m_receivedPackets{0};
    std::atomic<int> m_decodedFrames{0};
    std::atomic<int> m_decodeErrors{0};
    
    QMutex m_mutex;
    std::list<QByteArray> m_queue;
};

#endif // VIDEODECODER_H
