#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

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

        AVCodecParameters* codecParams = m_fmtCTX->streams[m_videoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        m_codecCTX = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_codecCTX, codecParams);
        if (avcodec_open2(m_codecCTX, codec, nullptr) < 0)
            return false;

        m_width = codecParams->width;
        m_height = codecParams->height;

        // Get frame rate from stream
        AVRational frame_rate = m_fmtCTX->streams[m_videoStreamIndex]->avg_frame_rate;
        if (frame_rate.num > 0 && frame_rate.den > 0)
        {
            m_frameDuration = std::chrono::duration<double>(static_cast<double>(frame_rate.den) / frame_rate.num);
        }
        else
        {
            // Fallback to 30 FPS if frame rate is not available
            m_frameDuration = std::chrono::duration<double>(1.0 / 30.0);
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
        frame_buffer.resize(num_bytes);

        m_startTime = std::chrono::steady_clock::now();
        m_hasNewFrame = false;

        return true;
    }

    uint8_t* grab_next_frame()
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - m_lastFrameTime;

        // Only decode a new frame if enough time has passed
        if (elapsed < m_frameDuration)
        {
            // Return the current frame buffer if we have a valid frame
            return m_hasNewFrame ? frame_buffer.data() : nullptr;
        }

        while (av_read_frame(m_fmtCTX, m_packet) >= 0)
        {
            if (m_packet->stream_index == m_videoStreamIndex)
            {
                if (avcodec_send_packet(m_codecCTX, m_packet) == 0)
                {
                    if (avcodec_receive_frame(m_codecCTX, m_frame) == 0)
                    {
                        uint8_t* dest[4] = {frame_buffer.data(), NULL, NULL, NULL};
                        int dest_linesize[4] = {m_width * 4, 0, 0, 0};
                        sws_scale(m_swsCTX, m_frame->data, m_frame->linesize, 0, m_height, dest, dest_linesize);

                        av_packet_unref(m_packet);
                        m_lastFrameTime = now;
                        m_hasNewFrame = true;
                        return frame_buffer.data();
                    }
                }
            }
            av_packet_unref(m_packet);
        }
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
    }

    int get_width() const
    {
        return m_width;
    }
    int get_height() const
    {
        return m_height;
    }

  private:
    AVFormatContext* m_fmtCTX{};
    AVCodecContext* m_codecCTX{};
    SwsContext* m_swsCTX{};
    AVFrame* m_frame{};
    AVPacket* m_packet{};
    int32_t m_videoStreamIndex{-1};
    int32_t m_width{};
    int32_t m_height{};
    std::vector<uint8_t> frame_buffer{};

    // Frame timing
    std::chrono::steady_clock::time_point m_startTime{};
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::chrono::duration<double> m_frameDuration{1.0 / 30.0};
    bool m_hasNewFrame{false};
};