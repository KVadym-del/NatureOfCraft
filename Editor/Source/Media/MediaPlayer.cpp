#include "MediaPlayer.hpp"
#include <iostream>

// --- Miniaudio Implementation ---
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
// --------------------------------

MediaPlayer::MediaPlayer()
{
    avformat_network_init();
    audio_device = new ma_device();
}

MediaPlayer::~MediaPlayer()
{
    stop();
    if (audio_device_ready)
    {
        ma_device_uninit(audio_device);
    }
    delete audio_device;
}

bool MediaPlayer::open(const std::string& filepath)
{
    stop();

    if (avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr) != 0)
    {
        std::cerr << "Could not open file: " << filepath << std::endl;
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
        return false;

    video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    video_frame = av_frame_alloc();
    has_pending_frame = false;

    // --- Init Video ---
    if (video_stream_idx >= 0)
    {
        AVCodecParameters* params = fmt_ctx->streams[video_stream_idx]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        video_codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(video_codec_ctx, params);
        avcodec_open2(video_codec_ctx, codec, nullptr);

        sws_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt,
                                 video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);

        rgba_buffer.resize(video_codec_ctx->width * video_codec_ctx->height * 4);
    }

    // --- Init Audio (Miniaudio + Swresample) ---
    if (audio_stream_idx >= 0)
    {
        AVCodecParameters* params = fmt_ctx->streams[audio_stream_idx]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        audio_codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(audio_codec_ctx, params);
        avcodec_open2(audio_codec_ctx, codec, nullptr);

        // 1. Configure Miniaudio
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16; // Standard signed 16-bit
        config.playback.channels = 2;           // Force Stereo
        config.sampleRate = 44100;              // Standard Sample Rate
        config.dataCallback = ma_data_callback;
        config.pUserData = this;

        if (ma_device_init(NULL, &config, audio_device) != MA_SUCCESS)
        {
            std::cerr << "Failed to init audio device." << std::endl;
            return false;
        }
        audio_device_ready = true;

        // 2. Configure Swresample (Source -> Target)
        // Target MUST match the miniaudio config above
        swr_ctx = swr_alloc();

        // Input settings (from FFmpeg)
        av_opt_set_chlayout(swr_ctx, "in_chlayout", &audio_codec_ctx->ch_layout, 0);
        av_opt_set_int(swr_ctx, "in_sample_rate", audio_codec_ctx->sample_rate, 0);
        av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_codec_ctx->sample_fmt, 0);

        // Output settings (must match Miniaudio config)
        AVChannelLayout out_layout;
        av_channel_layout_default(&out_layout, 2); // Stereo
        av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_layout, 0);
        av_opt_set_int(swr_ctx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

        swr_init(swr_ctx);
        av_channel_layout_uninit(&out_layout);

        audio_intermediate_buf.resize(192000);
    }

    return true;
}

void MediaPlayer::play()
{
    if (playing)
        return;
    playing = true;
    stop_threads = false;

    demuxer_thread = std::thread(&MediaPlayer::read_packets_thread, this);

    if (audio_device_ready)
    {
        ma_device_start(audio_device);
    }
}

void MediaPlayer::stop()
{
    playing = false;
    stop_threads = true;

    if (demuxer_thread.joinable())
        demuxer_thread.join();

    if (audio_device_ready)
    {
        ma_device_stop(audio_device);
        ma_device_uninit(audio_device);
        audio_device_ready = false;
    }

    cleanup();

    video_packet_queue.clear();
    audio_packet_queue.clear();
    audio_clock = 0.0;
    video_clock = 0.0;
}

void MediaPlayer::cleanup()
{
    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (swr_ctx)
    {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }
    if (video_codec_ctx)
    {
        avcodec_free_context(&video_codec_ctx);
        video_codec_ctx = nullptr;
    }
    if (audio_codec_ctx)
    {
        avcodec_free_context(&audio_codec_ctx);
        audio_codec_ctx = nullptr;
    }
    if (fmt_ctx)
    {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
    if (video_frame)
    {
        av_frame_free(&video_frame);
        video_frame = nullptr;
    }
}

void MediaPlayer::toggle_playback()
{
    if (playing)
    {
        playing = false;
        if (audio_device_ready)
            ma_device_stop(audio_device);
    }
    else
    {
        playing = true;
        if (audio_device_ready)
            ma_device_start(audio_device);
    }
}

void MediaPlayer::read_packets_thread()
{
    AVPacket* packet = av_packet_alloc();
    while (!stop_threads)
    {
        if (!playing || (video_packet_queue.size() > 100 && audio_packet_queue.size() > 100))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(fmt_ctx, packet);
        if (ret >= 0)
        {
            if (packet->stream_index == video_stream_idx)
            {
                video_packet_queue.push(av_packet_clone(packet));
            }
            else if (packet->stream_index == audio_stream_idx)
            {
                audio_packet_queue.push(av_packet_clone(packet));
            }
            av_packet_unref(packet);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    av_packet_free(&packet);
}

// --- Main Thread Update (Video Sync) ---
void MediaPlayer::update()
{
    if (!playing || !has_video())
        return;

    // We loop to potentially drop multiple frames if we are very far behind
    int max_loops = 5;
    while (max_loops > 0)
    {
        max_loops--;

        // 1. If we don't have a decoded frame waiting, try to decode one
        if (!has_pending_frame)
        {
            int ret = avcodec_receive_frame(video_codec_ctx, video_frame);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                // Codec needs more data, feed it a packet
                AVPacket* packet = nullptr;
                if (video_packet_queue.pop(packet))
                {
                    if (avcodec_send_packet(video_codec_ctx, packet) < 0)
                    {
                        // Error sending packet
                        av_packet_free(&packet);
                        return;
                    }
                    av_packet_free(&packet);
                }
                // We fed a packet, but we don't have a frame yet.
                // Return and check again next update cycle.
                return;
            }
            else if (ret >= 0)
            {
                // Successfully got a frame!
                has_pending_frame = true;
            }
            else
            {
                return; // Codec error
            }
        }

        // 2. Synchronization Check
        if (has_pending_frame)
        {
            double pts = video_frame->pts * av_q2d(fmt_ctx->streams[video_stream_idx]->time_base);
            double master_clock = audio_clock.load();
            double diff = pts - master_clock;

            // Sync Threshold (e.g., 30ms)
            double sync_threshold = 0.03;

            if (diff > sync_threshold)
            {
                // Case A: Video is AHEAD of audio (Future frame).
                // We are too fast. Do NOT display.
                // Return immediately to keep this frame for the next update call.
                return;
            }
            else if (diff < -sync_threshold)
            {
                // Case B: Video is BEHIND audio (Late frame).
                // We are too slow. Drop this frame and loop again to get the next one immediately.
                has_pending_frame = false;
                continue;
            }
            else
            {
                // Case C: On time! (within threshold)
                // Convert to RGBA
                uint8_t* dest[4] = {rgba_buffer.data(), nullptr, nullptr, nullptr};
                int linesize[4] = {video_codec_ctx->width * 4, 0, 0, 0};

                sws_scale(sws_ctx, video_frame->data, video_frame->linesize, 0, video_codec_ctx->height, dest,
                          linesize);

                // Mark as ready for Vulkan
                new_frame_ready = true;
                video_clock = pts;

                // Consume the frame
                has_pending_frame = false;
                return;
            }
        }
    }
}

uint8_t* MediaPlayer::grab_video_frame()
{
    if (new_frame_ready)
    {
        new_frame_ready = false;
        return rgba_buffer.data();
    }
    return nullptr;
}

// --- Audio Handling (Miniaudio Callback) ---

// Static callback wrapper
void MediaPlayer::ma_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{
    MediaPlayer* player = static_cast<MediaPlayer*>(pDevice->pUserData);
    if (!player || !player->playing)
    {
        memset(pOutput, 0, frameCount * 2 * sizeof(int16_t)); // Silence
        return;
    }

    // miniaudio asks for 'frameCount'. Since we set S16 Stereo:
    // bytes = frameCount * 2 (channels) * 2 (bytes per sample)
    int bytes_needed = frameCount * 2 * 2;
    player->read_audio_data((uint8_t*)pOutput, bytes_needed);
}

void MediaPlayer::read_audio_data(uint8_t* stream, int len)
{
    int len_remaining = len;
    int current_stream_pos = 0;

    while (len_remaining > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            int decoded = decode_audio_frame(audio_intermediate_buf.data(), audio_intermediate_buf.size());
            if (decoded < 0)
            {
                memset(stream + current_stream_pos, 0, len_remaining);
                return;
            }
            audio_buf_size = decoded;
            audio_buf_index = 0;
        }

        int len_to_copy = audio_buf_size - audio_buf_index;
        if (len_to_copy > len_remaining)
            len_to_copy = len_remaining;

        memcpy(stream + current_stream_pos, audio_intermediate_buf.data() + audio_buf_index, len_to_copy);

        len_remaining -= len_to_copy;
        current_stream_pos += len_to_copy;
        audio_buf_index += len_to_copy;
    }
}

int MediaPlayer::decode_audio_frame(uint8_t* buffer, int buffer_size)
{
    AVPacket* packet = nullptr;
    AVFrame* frame = av_frame_alloc();
    int result_size = -1;

    while (result_size < 0)
    {
        int ret = avcodec_receive_frame(audio_codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            if (!audio_packet_queue.pop(packet))
            {
                av_frame_free(&frame);
                return -1;
            }
            if (avcodec_send_packet(audio_codec_ctx, packet) < 0)
            {
                av_packet_free(&packet);
                av_frame_free(&frame);
                return -1;
            }
            av_packet_free(&packet);
            continue;
        }
        else if (ret < 0)
        {
            av_frame_free(&frame);
            return -1;
        }

        if (frame->pts != AV_NOPTS_VALUE)
        {
            audio_clock = frame->pts * av_q2d(fmt_ctx->streams[audio_stream_idx]->time_base);
        }

        // Convert whatever FFmpeg gave us to standard Stereo S16 for Miniaudio
        uint8_t* out_buffer_ptr = buffer;
        int converted_samples =
            swr_convert(swr_ctx, &out_buffer_ptr, buffer_size / 4, (const uint8_t**)frame->data, frame->nb_samples);

        if (converted_samples > 0)
        {
            result_size = converted_samples * 2 * 2; // samples * channels * bytes
        }
    }
    av_frame_free(&frame);
    return result_size;
}

// Getters
bool MediaPlayer::has_video() const
{
    return video_stream_idx >= 0;
}
bool MediaPlayer::has_audio() const
{
    return audio_stream_idx >= 0;
}
int MediaPlayer::get_video_width() const
{
    return video_codec_ctx ? video_codec_ctx->width : 0;
}
int MediaPlayer::get_video_height() const
{
    return video_codec_ctx ? video_codec_ctx->height : 0;
}
bool MediaPlayer::is_playing() const
{
    return playing;
}
double MediaPlayer::get_playback_clock()
{
    return audio_clock;
}
size_t MediaPlayer::get_video_queue_size()
{
    return video_packet_queue.size();
}
size_t MediaPlayer::get_audio_buffer_size()
{
    return audio_packet_queue.size();
}
int MediaPlayer::get_audio_sample_rate() const
{
    return audio_codec_ctx ? audio_codec_ctx->sample_rate : 0;
}