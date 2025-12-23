#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class VideoPlayer
{
  public:
    VideoPlayer() = default;
    inline ~VideoPlayer()
    {
        this->cleanup();
    }

    bool open(const std::filesystem::path& filepath)
    {
        if (avformat_open_input(&m_fmtCTX, filepath.string().c_str(), NULL, NULL) < 0)
            return false;
        if (avformat_find_stream_info(m_fmtCTX, NULL) < 0)
            return false;

        for (unsigned int i = 0; i < m_fmtCTX->nb_streams; i++)
        {
            if (m_fmtCTX->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                m_videoStreamIndex = i;
                break;
            }
        }
        if (m_videoStreamIndex == -1)
            return false;

        m_timeBase = av_q2d(m_fmtCTX->streams[m_videoStreamIndex]->time_base);

        AVCodecParameters* codecParams = m_fmtCTX->streams[m_videoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        m_codecCTX = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_codecCTX, codecParams);
        if (avcodec_open2(m_codecCTX, codec, nullptr) < 0)
            return false;

        m_width = codecParams->width;
        m_height = codecParams->height;

        AVRational frame_rate = m_fmtCTX->streams[m_videoStreamIndex]->avg_frame_rate;
        if (frame_rate.num > 0 && frame_rate.den > 0)
        {
            m_frameDuration = static_cast<double>(frame_rate.den) / frame_rate.num;
        }
        else
        {
            m_frameDuration = 1.0 / 30.0;
        }

        m_swsCTX = sws_getContext(
            m_width,
            m_height,
            m_codecCTX->pix_fmt,
            m_width,
            m_height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr, nullptr, nullptr
        );

        m_frame = av_frame_alloc();
        m_packet = av_packet_alloc();

        int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, m_width, m_height, 1);
        m_currentFrameBuffer.resize(num_bytes);

        m_isOpen = true;
        return true;
    }

    bool play()
    {
        if (!m_isOpen || m_isPlaying)
            return false;
        m_isPlaying = true;
        return true;
    }

    bool pause()
    {
        if (!m_isOpen || !m_isPlaying)
            return false;
        m_isPlaying = false;
        return true;
    }

    bool toggle_playback()
    {
        if (m_isPlaying)
            return pause();
        else
            return play();
    }

    bool stop()
    {
        if (!m_isOpen)
            return false;

        m_isPlaying = false;
        
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            while (!m_frameQueue.empty())
                m_frameQueue.pop();
        }

        if (m_fmtCTX && m_videoStreamIndex >= 0)
        {
            av_seek_frame(m_fmtCTX, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_codecCTX);
        }

        m_endOfFile = false;
        m_hasNewFrame = false;
        m_currentFramePts = 0.0;

        return true;
    }

    void decode_frames(int maxQueueSize = 10)
    {
        if (!m_isOpen || m_endOfFile)
            return;

        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            if (static_cast<int>(m_frameQueue.size()) >= maxQueueSize)
                return;
        }

        while (av_read_frame(m_fmtCTX, m_packet) >= 0)
        {
            if (m_packet->stream_index == m_videoStreamIndex)
            {
                if (avcodec_send_packet(m_codecCTX, m_packet) == 0)
                {
                    while (avcodec_receive_frame(m_codecCTX, m_frame) == 0)
                    {
                        double pts = 0.0;
                        if (m_frame->pts != AV_NOPTS_VALUE)
                        {
                            pts = m_frame->pts * m_timeBase;
                        }
                        else if (m_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        {
                            pts = m_frame->best_effort_timestamp * m_timeBase;
                        }

                        VideoFrame vf;
                        vf.pts = pts;
                        vf.data.resize(m_width * m_height * 4);

                        uint8_t* dest[4] = {vf.data.data(), NULL, NULL, NULL};
                        int dest_linesize[4] = {m_width * 4, 0, 0, 0};
                        sws_scale(m_swsCTX, m_frame->data, m_frame->linesize, 0, m_height, dest, dest_linesize);

                        {
                            std::lock_guard<std::mutex> lock(m_frameMutex);
                            m_frameQueue.push(std::move(vf));
                        }

                        av_frame_unref(m_frame);
                        av_packet_unref(m_packet);

                        {
                            std::lock_guard<std::mutex> lock(m_frameMutex);
                            if (static_cast<int>(m_frameQueue.size()) >= maxQueueSize)
                                return;
                        }
                    }
                }
            }
            av_packet_unref(m_packet);
        }

        m_endOfFile = true;
    }

    uint8_t* grab_frame_synced(double audioClock)
    {
        if (!m_isPlaying)
        {
            return m_hasNewFrame ? m_currentFrameBuffer.data() : nullptr;
        }

        std::lock_guard<std::mutex> lock(m_frameMutex);

        while (m_frameQueue.size() > 1)
        {
            const VideoFrame& front = m_frameQueue.front();
            const VideoFrame& next = m_frameQueue.size() > 1 ? 
                *(&front + 1) : front;

            if (!m_frameQueue.empty())
            {
                std::queue<VideoFrame> tempQueue = m_frameQueue;
                tempQueue.pop();
                if (!tempQueue.empty() && tempQueue.front().pts < audioClock - 0.01)
                {
                    m_frameQueue.pop();
                    continue;
                }
            }
            break;
        }

        if (!m_frameQueue.empty())
        {
            const VideoFrame& front = m_frameQueue.front();
            
            if (front.pts <= audioClock + 0.02)
            {
                std::memcpy(m_currentFrameBuffer.data(), front.data.data(), front.data.size());
                m_currentFramePts = front.pts;
                m_frameQueue.pop();
                m_hasNewFrame = true;
            }
        }

        return m_hasNewFrame ? m_currentFrameBuffer.data() : nullptr;
    }

    uint8_t* grab_next_frame()
    {
        if (!m_isPlaying)
        {
            return m_hasNewFrame ? m_currentFrameBuffer.data() : nullptr;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - m_lastFrameTime;

        if (elapsed < std::chrono::duration<double>(m_frameDuration))
        {
            return m_hasNewFrame ? m_currentFrameBuffer.data() : nullptr;
        }

        while (av_read_frame(m_fmtCTX, m_packet) >= 0)
        {
            if (m_packet->stream_index == m_videoStreamIndex)
            {
                if (avcodec_send_packet(m_codecCTX, m_packet) == 0)
                {
                    if (avcodec_receive_frame(m_codecCTX, m_frame) == 0)
                    {
                        uint8_t* dest[4] = {m_currentFrameBuffer.data(), NULL, NULL, NULL};
                        int dest_linesize[4] = {m_width * 4, 0, 0, 0};
                        sws_scale(m_swsCTX, m_frame->data, m_frame->linesize, 0, m_height, dest, dest_linesize);

                        av_packet_unref(m_packet);
                        m_lastFrameTime = now;
                        m_hasNewFrame = true;
                        return m_currentFrameBuffer.data();
                    }
                }
            }
            av_packet_unref(m_packet);
        }

        m_endOfFile = true;
        return nullptr;
    }

    void cleanup()
    {
        if (m_frame)
            av_frame_free(&m_frame);
        if (m_packet)
            av_packet_free(&m_packet);
        if (m_codecCTX)
            avcodec_free_context(&m_codecCTX);
        if (m_fmtCTX)
            avformat_close_input(&m_fmtCTX);
        if (m_swsCTX)
            sws_freeContext(m_swsCTX);

        m_isOpen = false;
        m_isPlaying = false;
    }

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    bool is_playing() const { return m_isPlaying; }
    bool is_open() const { return m_isOpen; }
    bool is_end_of_file() const { return m_endOfFile; }
    double get_current_pts() const { return m_currentFramePts; }
    
    size_t get_queue_size() const
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_frameQueue.size();
    }

  private:
    struct VideoFrame
    {
        double pts{0.0};
        std::vector<uint8_t> data;
    };

    AVFormatContext* m_fmtCTX{};
    AVCodecContext* m_codecCTX{};
    SwsContext* m_swsCTX{};
    AVFrame* m_frame{};
    AVPacket* m_packet{};
    int32_t m_videoStreamIndex{-1};
    int32_t m_width{};
    int32_t m_height{};
    double m_timeBase{0.0};
    double m_frameDuration{1.0 / 30.0};

    std::vector<uint8_t> m_currentFrameBuffer{};
    std::queue<VideoFrame> m_frameQueue;
    mutable std::mutex m_frameMutex;
    double m_currentFramePts{0.0};

    std::chrono::steady_clock::time_point m_lastFrameTime{};
    
    std::atomic<bool> m_isOpen{false};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_endOfFile{false};
    bool m_hasNewFrame{false};
};