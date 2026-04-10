#include "VideoDecoder.h"
#include <QDebug>

VideoDecoder::VideoDecoder(QObject *parent) : QThread(parent) {
    // 屏蔽FFmpeg过于多余的内部报错（如刚启动时必须等待I帧导致的 PPS 丢失错误）
    // 把日志级别调至 FATAL（或者 QUIET），彻底告别刷屏
    av_log_set_level(AV_LOG_FATAL);

    // 强制声明 H.265 (HEVC) 解码器
    m_codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    if (!m_codec) {
        qCritical() << "H.265/HEVC codec not found!";
        return;
    }

    m_codecCtx = avcodec_alloc_context3(m_codec);
    
    // 关键修正：开启 H.265 最强容错隐蔽和极低延迟解码功能
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    m_codecCtx->error_concealment = 3; // FF_EC_GUESS_MVS | FF_EC_DEBLOCK，丢包就自动猜图遮掩，不报花屏错
    m_codecCtx->thread_count = 2;      // 小开一下多线程加速解码吞吐

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        qCritical() << "Could not open H.265/HEVC codec!";
        return;
    }

    m_parser = av_parser_init(m_codec->id);
    if (!m_parser) {
        qCritical() << "Could not initialize HEVC parser!";
    }

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_rgbFrame = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    stop();
    wait();

    if (m_swsCtx) sws_freeContext(m_swsCtx);
    if (m_parser) {
        av_parser_close(m_parser);
    }
    if (m_frame) av_frame_free(&m_frame);
    if (m_rgbFrame) av_frame_free(&m_rgbFrame);
    if (m_packet) av_packet_free(&m_packet);
    if (m_codecCtx) {
        avcodec_close(m_codecCtx);
        avcodec_free_context(&m_codecCtx);
    }
}

void VideoDecoder::pushData(const QByteArray &data) {
    if (data.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    // 限定队列长度防止内存泄漏
    if (m_queue.size() > 100) {
        // 放宽队列到 100 帧（大约 3 秒以上缓冲），如果还是超出则只能丢弃最老的帧
        // 这在极端弱网/主板卡顿下才触发丢弃
        m_queue.pop_front();
    }
    m_queue.push_back(data);
}

void VideoDecoder::stop() {
    m_running = false;
}

void VideoDecoder::run() {
    while (m_running) {
        QByteArray data;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_queue.empty()) {
                data = m_queue.front();
                m_queue.pop_front();
            }
        }

        if (data.isEmpty()) {
            QThread::msleep(1); // 降低休眠延迟，原先5ms可能会引起缓冲区积压
            continue;
        }

        processData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    }
}

void VideoDecoder::processData(const uint8_t *data, int size) {
    if (size <= 0 || !m_parser || !m_codecCtx) return;

    // 使用 FFmpeg 官方推荐的流解析器 (Bitstream Parser) 切割 H.265 的 NALU 单元
    // 这能让从网络收到的零散、拼凑哪怕残缺的字节流，重新组装成解码器认可的标准帧，
    // 彻底根除 "Two slices reporting being the first in the same frame" 和 "PPS id out of range" 的报错！
    uint8_t *data_ptr = const_cast<uint8_t*>(data);
    int data_size = size;

    while (data_size > 0) {
        int parsed_len = av_parser_parse2(m_parser, m_codecCtx,
                                          &m_packet->data, &m_packet->size,
                                          data_ptr, data_size,
                                          AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (parsed_len < 0) {
            break; // 遇到无可救药的解析错误则跳过该包
        }

        data_ptr += parsed_len;
        data_size -= parsed_len;

        // 如果 parser 从碎流中分析出并产生了一个完整的 AVPacket
        if (m_packet->size > 0) {
            int ret = avcodec_send_packet(m_codecCtx, m_packet);
            // 这里即使送包失败或者返回 EAGAIN，也务必走一遍 receive_frame 疏通解码器缓存池，否则极易发生堆积段错误
            while (true) {
                int recv_ret = avcodec_receive_frame(m_codecCtx, m_frame);
                if (recv_ret != 0) {
                    break; // 读不到(EAGAIN/EOF等)即可跳出循环
                }

                int w = m_frame->width;
                int h = m_frame->height;

                // 防止解码器吐出空帧或格式错误，引起 swscaler 崩溃 (Slice parameters 0 are invalid)
                if (w <= 0 || h <= 0 || !m_frame->data[0]) {
                    av_frame_unref(m_frame);
                    continue;
                }

                // 如果尺寸或者像素格式变了，必须需要重置转换器，否则 sws_scale 段错误
                if (m_width != w || m_height != h || m_pixFmt != m_frame->format || !m_swsCtx) {
                    m_width = w;
                    m_height = h;
                    m_pixFmt = m_frame->format;
                    if (m_swsCtx) {
                        sws_freeContext(m_swsCtx);
                    }

                    m_swsCtx = sws_getContext(w, h, (AVPixelFormat)m_frame->format,
                                              w, h, AV_PIX_FMT_RGB32, 
                                              SWS_BILINEAR, nullptr, nullptr, nullptr);
                }

                if (!m_swsCtx) {
                    av_frame_unref(m_frame);
                    continue;
                }

                QImage image(w, h, QImage::Format_RGB32);
                uint8_t *dest[4] = {image.bits(), nullptr, nullptr, nullptr};
                int dest_linesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

                int ret_scale = sws_scale(m_swsCtx, m_frame->data, m_frame->linesize, 0, h,
                                          dest, dest_linesize);

                av_frame_unref(m_frame); 

                if (ret_scale > 0) {
                    emit frameReady(image.copy()); // 安全深拷贝发送到主线程
                }
            }
        }
    }
}
