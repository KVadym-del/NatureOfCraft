#pragma once
#include "VideoPlayer.hpp"
#include "AudioPlayer.hpp"
#include <filesystem>
#include <memory>
#include <chrono>

class MediaPlayer
{
public:
    MediaPlayer() = default;

    inline ~MediaPlayer()
    {
        cleanup();
    }

    inline bool open(const std::filesystem::path& filepath)
    {
        m_filepath = filepath;

        m_videoPlayer = std::make_unique<VideoPlayer>();
        m_hasVideo = m_videoPlayer->open(filepath);
        if (!m_hasVideo)
        {
            m_videoPlayer.reset();
        }

        m_audioPlayer = std::make_unique<AudioPlayer>();
        m_hasAudio = m_audioPlayer->open(filepath);
        if (!m_hasAudio)
        {
            m_audioPlayer.reset();
        }

        m_isOpen = m_hasVideo || m_hasAudio;
        
        if (!m_hasAudio && m_hasVideo)
        {
            m_useSystemClock = true;
        }

        return m_isOpen;
    }

    inline bool play()
    {
        if (!m_isOpen || m_isPlaying)
            return false;

        bool success = true;

        if (m_hasVideo)
            success &= m_videoPlayer->play();

        if (m_hasAudio)
            success &= m_audioPlayer->play();

        if (success)
        {
            m_isPlaying = true;
            if (m_useSystemClock)
            {
                m_playbackStartTime = std::chrono::steady_clock::now();
            }
        }

        return success;
    }

    inline bool pause()
    {
        if (!m_isOpen || !m_isPlaying)
            return false;

        bool success = true;

        if (m_hasVideo)
            success &= m_videoPlayer->pause();

        if (m_hasAudio)
            success &= m_audioPlayer->pause();

        if (success)
        {
            m_isPlaying = false;
            if (m_useSystemClock)
            {
                auto now = std::chrono::steady_clock::now();
                m_pausedTime += std::chrono::duration<double>(now - m_playbackStartTime).count();
            }
        }

        return success;
    }

    inline bool toggle_playback()
    {
        if (m_isPlaying)
            return pause();
        else
            return play();
    }

    inline bool stop()
    {
        if (!m_isOpen)
            return false;

        bool success = true;

        if (m_hasVideo)
            success &= m_videoPlayer->stop();

        if (m_hasAudio)
        {
            m_audioPlayer->pause();
            m_audioPlayer->cleanup();
            m_hasAudio = m_audioPlayer->open(m_filepath);
        }

        m_isPlaying = false;
        m_pausedTime = 0.0;
        return success;
    }

    inline void update()
    {
        if (!m_isOpen)
            return;

        if (m_hasAudio && m_audioPlayer)
        {
            m_audioPlayer->decode_audio_frames();
        }

        if (m_hasVideo && m_videoPlayer)
        {
            m_videoPlayer->decode_frames(10);
        }
    }

    inline uint8_t* grab_video_frame()
    {
        if (!m_hasVideo || !m_videoPlayer)
            return nullptr;

        double clock = get_playback_clock();

        return m_videoPlayer->grab_frame_synced(clock);
    }

    inline double get_playback_clock() const
    {
        if (m_hasAudio && m_audioPlayer && m_audioPlayer->is_playing())
        {
            return m_audioPlayer->get_audio_clock();
        }
        else if (m_useSystemClock && m_isPlaying)
        {
            auto now = std::chrono::steady_clock::now();
            return m_pausedTime + std::chrono::duration<double>(now - m_playbackStartTime).count();
        }
        
        return m_pausedTime;
    }

    inline void cleanup()
    {
        if (m_videoPlayer)
        {
            m_videoPlayer->cleanup();
            m_videoPlayer.reset();
        }

        if (m_audioPlayer)
        {
            m_audioPlayer->cleanup();
            m_audioPlayer.reset();
        }

        m_isOpen = false;
        m_isPlaying = false;
        m_hasVideo = false;
        m_hasAudio = false;
    }
    
    public:
    inline bool is_open() const
    {
        return m_isOpen;
    }
    inline bool is_playing() const
    {
        return m_isPlaying;
    }
    inline bool has_video() const
    {
        return m_hasVideo;
    }
    inline bool has_audio() const
    {
        return m_hasAudio;
    }

    inline bool is_end_of_file() const
    {
        bool videoEnded = !m_hasVideo || (m_videoPlayer && m_videoPlayer->is_end_of_file());
        bool audioEnded = !m_hasAudio || (m_audioPlayer && m_audioPlayer->is_end_of_file());
        return videoEnded && audioEnded;
    }

    inline int get_video_width() const
    {
        return m_hasVideo && m_videoPlayer ? m_videoPlayer->get_width() : 0;
    }

    inline int get_video_height() const
    {
        return m_hasVideo && m_videoPlayer ? m_videoPlayer->get_height() : 0;
    }

    inline int get_audio_sample_rate() const
    {
        return m_hasAudio && m_audioPlayer ? m_audioPlayer->get_sample_rate() : 0;
    }

    inline int get_audio_channels() const
    {
        return m_hasAudio && m_audioPlayer ? m_audioPlayer->get_channels() : 0;
    }

    inline size_t get_audio_buffer_size() const
    {
        return m_hasAudio && m_audioPlayer ? m_audioPlayer->get_buffer_size() : 0;
    }

    inline size_t get_video_queue_size() const
    {
        return m_hasVideo && m_videoPlayer ? m_videoPlayer->get_queue_size() : 0;
    }

    inline VideoPlayer* get_video_player() { return m_videoPlayer.get(); }
    inline AudioPlayer* get_audio_player() { return m_audioPlayer.get(); }

private:
    std::filesystem::path m_filepath{};
    std::unique_ptr<VideoPlayer> m_videoPlayer{};
    std::unique_ptr<AudioPlayer> m_audioPlayer{};

    bool m_isOpen{false};
    bool m_isPlaying{false};
    bool m_hasVideo{false};
    bool m_hasAudio{false};
    bool m_useSystemClock{false};

    std::chrono::steady_clock::time_point m_playbackStartTime{};
    double m_pausedTime{0.0};
};
