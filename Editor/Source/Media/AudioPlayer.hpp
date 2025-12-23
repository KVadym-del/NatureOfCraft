#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <queue>
#include <mutex>
#include <atomic>
#include <cstring>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <portaudio.h>

class AudioPlayer
{
public:
    AudioPlayer() = default;

    inline ~AudioPlayer()
    {
        cleanup();
    }

    inline bool open(const std::filesystem::path& filepath)
    {
        if (avformat_open_input(&m_fmtCTX, filepath.string().c_str(), nullptr, nullptr) < 0)
            return false;

        if (avformat_find_stream_info(m_fmtCTX, nullptr) < 0)
            return false;

        for (unsigned int i = 0; i < m_fmtCTX->nb_streams; i++)
        {
            if (m_fmtCTX->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                m_audioStreamIndex = i;
                break;
            }
        }

        if (m_audioStreamIndex == -1)
            return false;

        m_timeBase = av_q2d(m_fmtCTX->streams[m_audioStreamIndex]->time_base);

        AVCodecParameters* codecParams = m_fmtCTX->streams[m_audioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec)
            return false;

        m_codecCTX = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(m_codecCTX, codecParams) < 0)
            return false;

        if (avcodec_open2(m_codecCTX, codec, nullptr) < 0)
            return false;

        m_sampleRate = m_codecCTX->sample_rate;
        m_channels = m_codecCTX->ch_layout.nb_channels;

        m_swrCTX = swr_alloc();
        if (!m_swrCTX)
            return false;

        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, m_channels);

        if (swr_alloc_set_opts2(&m_swrCTX,
                &outLayout,
                AV_SAMPLE_FMT_FLT,
                m_sampleRate,
                &m_codecCTX->ch_layout,
                m_codecCTX->sample_fmt,
                m_codecCTX->sample_rate,
                0, nullptr) < 0)
        {
            av_channel_layout_uninit(&outLayout);
            return false;
        }

        av_channel_layout_uninit(&outLayout);

        if (swr_init(m_swrCTX) < 0)
            return false;

        m_frame = av_frame_alloc();
        m_packet = av_packet_alloc();

        PaError err = Pa_Initialize();
        if (err != paNoError)
        {
            std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        m_paInitialized = true;

        err = Pa_OpenDefaultStream(
            &m_stream,
            0,            
            m_channels,   
            paFloat32,    
            m_sampleRate, 
            256,          
            audioCallback,
            this          
        );

        if (err != paNoError)
        {
            std::cerr << "PortAudio stream open failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        m_isOpen = true;
        return true;
    }

    inline bool play()
    {
        if (!m_isOpen || m_isPlaying)
            return false;

        PaError err = Pa_StartStream(m_stream);
        if (err != paNoError)
        {
            std::cerr << "PortAudio stream start failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        m_isPlaying = true;
        return true;
    }

    inline bool pause()
    {
        if (!m_isOpen || !m_isPlaying)
            return false;

        PaError err = Pa_StopStream(m_stream);
        if (err != paNoError)
        {
            std::cerr << "PortAudio stream stop failed: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        m_isPlaying = false;
        return true;
    }

    inline bool toggle_playback()
    {
        if (m_isPlaying)
            return pause();
        else
            return play();
    }

    inline void decode_audio_frames()
    {
        if (!m_isOpen)
            return;

        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_audioBuffer.size() > m_sampleRate * m_channels * 2)
                return;
        }

        while (av_read_frame(m_fmtCTX, m_packet) >= 0)
        {
            if (m_packet->stream_index == m_audioStreamIndex)
            {
                if (avcodec_send_packet(m_codecCTX, m_packet) == 0)
                {
                    while (avcodec_receive_frame(m_codecCTX, m_frame) == 0)
                    {
                        double framePts = 0.0;
                        if (m_frame->pts != AV_NOPTS_VALUE)
                        {
                            framePts = m_frame->pts * m_timeBase;
                        }

                        int outSamples = swr_get_out_samples(m_swrCTX, m_frame->nb_samples);
                        
                        std::vector<float> outBuffer(outSamples * m_channels);
                        uint8_t* outPtr = reinterpret_cast<uint8_t*>(outBuffer.data());

                        int convertedSamples = swr_convert(
                            m_swrCTX,
                            &outPtr,
                            outSamples,
                            const_cast<const uint8_t**>(m_frame->data),
                            m_frame->nb_samples
                        );

                        if (convertedSamples > 0)
                        {
                            std::lock_guard<std::mutex> lock(m_bufferMutex);
                            
                            if (m_audioBuffer.empty())
                            {
                                m_bufferStartPts = framePts;
                            }

                            for (int i = 0; i < convertedSamples * m_channels; i++)
                            {
                                m_audioBuffer.push(outBuffer[i]);
                            }
                        }

                        av_frame_unref(m_frame);

                        {
                            std::lock_guard<std::mutex> lock(m_bufferMutex);
                            if (m_audioBuffer.size() > m_sampleRate * m_channels * 2)
                            {
                                av_packet_unref(m_packet);
                                return;
                            }
                        }
                    }
                }
            }
            av_packet_unref(m_packet);
        }

        m_endOfFile = true;
    }

    inline double get_audio_clock() const
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        double samplesInBuffer = static_cast<double>(m_audioBuffer.size()) / m_channels;
        double bufferDuration = samplesInBuffer / m_sampleRate;
        return m_currentPts - bufferDuration;
    }

    inline void cleanup()
    {
        if (m_isPlaying)
        {
            Pa_StopStream(m_stream);
            m_isPlaying = false;
        }

        if (m_stream)
        {
            Pa_CloseStream(m_stream);
            m_stream = nullptr;
        }

        if (m_paInitialized)
        {
            Pa_Terminate();
            m_paInitialized = false;
        }

        if (m_frame)
            av_frame_free(&m_frame);
        if (m_packet)
            av_packet_free(&m_packet);
        if (m_codecCTX)
            avcodec_free_context(&m_codecCTX);
        if (m_fmtCTX)
            avformat_close_input(&m_fmtCTX);
        if (m_swrCTX)
            swr_free(&m_swrCTX);

        m_isOpen = false;
    }

    inline bool is_playing() const
    {
        return m_isPlaying;
    }
    inline bool is_open() const
    {
        return m_isOpen;
    }
    inline bool is_end_of_file() const
    {
        return m_endOfFile && m_audioBuffer.empty();
    }
    inline int get_sample_rate() const
    {
        return m_sampleRate;
    }
    inline int get_channels() const
    {
        return m_channels;
    }

    inline size_t get_buffer_size() const
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        return m_audioBuffer.size();
    }

private:
    inline static int audioCallback(
        const void* inputBuffer,
        void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData)
    {
        (void)inputBuffer;
        (void)timeInfo;
        (void)statusFlags;

        AudioPlayer* player = static_cast<AudioPlayer*>(userData);
        float* out = static_cast<float*>(outputBuffer);

        std::lock_guard<std::mutex> lock(player->m_bufferMutex);

        unsigned long samplesNeeded = framesPerBuffer * player->m_channels;
        unsigned long samplesWritten = 0;

        for (unsigned long i = 0; i < samplesNeeded; i++)
        {
            if (!player->m_audioBuffer.empty())
            {
                out[i] = player->m_audioBuffer.front();
                player->m_audioBuffer.pop();
                samplesWritten++;
            }
            else
            {
                out[i] = 0.0f;
            }
        }

        if (samplesWritten > 0)
        {
            double samplesConsumed = static_cast<double>(samplesWritten) / player->m_channels;
            player->m_currentPts += samplesConsumed / player->m_sampleRate;
        }

        return paContinue;
    }

    AVFormatContext* m_fmtCTX{nullptr};
    AVCodecContext* m_codecCTX{nullptr};
    SwrContext* m_swrCTX{nullptr};
    AVFrame* m_frame{nullptr};
    AVPacket* m_packet{nullptr};
    int32_t m_audioStreamIndex{-1};

    int m_sampleRate{44100};
    int m_channels{2};
    double m_timeBase{0.0};

    PaStream* m_stream{nullptr};
    bool m_paInitialized{false};

    std::queue<float> m_audioBuffer{};
    mutable std::mutex m_bufferMutex{};

    double m_bufferStartPts{0.0};
    double m_currentPts{0.0};

    std::atomic<bool> m_isOpen{false};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_endOfFile{false};
};
