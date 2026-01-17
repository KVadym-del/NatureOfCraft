#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// Include miniaudio header in the CPP file,
// strictly use struct pointers here to keep compile times fast.
struct ma_device;
struct ma_context;

// Helper struct for thread-safe queue (Same as before)
template <typename T> class SafeQueue
{
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond;

  public:
    void push(T item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(item);
        cond.notify_one();
    }
    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (queue.empty())
            return false;
        item = queue.front();
        queue.pop();
        return true;
    }
    size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::queue<T> empty;
        std::swap(queue, empty);
    }
};

class MediaPlayer
{
  public:
    MediaPlayer();
    ~MediaPlayer();

    bool open(const std::string& filepath);
    void play();
    void stop();
    void toggle_playback();
    void update();

    // Video Access
    bool has_video() const;
    int get_video_width() const;
    int get_video_height() const;
    uint8_t* grab_video_frame();
    size_t get_video_queue_size();

    // Audio Access
    bool has_audio() const;
    int get_audio_sample_rate() const;
    size_t get_audio_buffer_size();

    // State
    bool is_playing() const;
    double get_playback_clock();

  private:
    void cleanup();
    void read_packets_thread();

    // Miniaudio Callback
    static void ma_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
    void read_audio_data(uint8_t* stream, int len);

    int decode_audio_frame(uint8_t* buffer, int buffer_size);

    AVFormatContext* fmt_ctx = nullptr;

    // Video
    int video_stream_idx = -1;
    AVCodecContext* video_codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SafeQueue<AVPacket*> video_packet_queue;
    std::vector<uint8_t> rgba_buffer;
    std::atomic<bool> new_frame_ready{false};

    AVFrame* video_frame = nullptr;
    bool has_pending_frame = false;

    // Audio
    int audio_stream_idx = -1;
    AVCodecContext* audio_codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    SafeQueue<AVPacket*> audio_packet_queue;

    // Miniaudio structures
    ma_device* audio_device = nullptr;
    bool audio_device_ready = false;

    // Synchronization & State
    std::atomic<bool> playing{false};
    std::atomic<bool> stop_threads{false};
    std::thread demuxer_thread;

    std::vector<uint8_t> audio_intermediate_buf;
    size_t audio_buf_index = 0;
    size_t audio_buf_size = 0;

    std::atomic<double> audio_clock{0.0};
    double video_clock = 0.0;
};