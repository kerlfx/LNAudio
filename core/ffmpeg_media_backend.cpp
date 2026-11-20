#include "ffmpeg_media_backend.h"

#ifdef _WIN32
#include "wasapi_audio_output.h"
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace lnplayer
{
namespace
{

constexpr int kOutputSampleRate = 48000;
constexpr int kOutputChannels = 2;

std::string ffmpeg_error(int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

class FFmpegMediaBackend final : public MediaBackend
{
public:
    ~FFmpegMediaBackend() override
    {
        close();
    }

    MediaBackendResult open(const std::string& source, MediaInfo& info) override
    {
        close();

        int result = avformat_open_input(&format_context_, source.c_str(), nullptr, nullptr);
        if (result < 0)
            return failure("Unable to open media source: " + ffmpeg_error(result));

        result = avformat_find_stream_info(format_context_, nullptr);
        if (result < 0)
        {
            const std::string message =
                "Unable to read media stream information: " + ffmpeg_error(result);
            close();
            return failure(message);
        }

        audio_stream_index_ = av_find_best_stream(
            format_context_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        video_stream_index_ = av_find_best_stream(
            format_context_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        info.has_audio = audio_stream_index_ >= 0;
        info.has_video = video_stream_index_ >= 0;
        info.duration_ms = format_context_->duration == AV_NOPTS_VALUE
            ? -1
            : av_rescale_q(
                format_context_->duration,
                AVRational{1, AV_TIME_BASE},
                AVRational{1, 1000});

        if (!info.has_audio && !info.has_video)
        {
            close();
            return failure("The media source contains no supported audio or video stream.");
        }

        if (info.has_audio)
        {
            AVStream* stream = format_context_->streams[audio_stream_index_];
            const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder)
            {
                close();
                return failure("No FFmpeg decoder is available for the audio stream.");
            }

            audio_decoder_ = avcodec_alloc_context3(decoder);
            if (!audio_decoder_)
            {
                close();
                return failure("Unable to allocate the audio decoder.");
            }

            result = avcodec_parameters_to_context(audio_decoder_, stream->codecpar);
            if (result < 0)
            {
                const std::string message =
                    "Unable to configure the audio decoder: " + ffmpeg_error(result);
                close();
                return failure(message);
            }

            result = avcodec_open2(audio_decoder_, decoder, nullptr);
            if (result < 0)
            {
                const std::string message =
                    "Unable to open the audio decoder: " + ffmpeg_error(result);
                close();
                return failure(message);
            }

            audio_packet_ = av_packet_alloc();
            audio_frame_ = av_frame_alloc();
            if (!audio_packet_ || !audio_frame_)
            {
                close();
                return failure("Unable to allocate FFmpeg audio decode buffers.");
            }
        }

        opened_ = true;
        position_ms_.store(0, std::memory_order_relaxed);
        playback_ended_.store(false, std::memory_order_relaxed);
        audio_draining_ = false;
        audio_packet_pending_ = false;
        return success();
    }

    MediaBackendResult play() override
    {
        if (!opened_)
            return failure("No media source is open.");
#ifndef _WIN32
        return failure("The current platform has no configured audio output.");
#else
        if (!audio_decoder_)
            return failure("This media source has no audio stream.");

        std::unique_lock<std::mutex> lock(playback_mutex_);
        if (!playback_thread_.joinable())
        {
            playback_start_finished_ = false;
            playback_start_success_ = false;
            playback_stop_ = false;
            try
            {
                playback_thread_ = std::thread([this] { audio_playback_loop(); });
            }
            catch (...)
            {
                return failure("Unable to start the audio playback thread.");
            }

            playback_condition_.wait(lock, [this] { return playback_start_finished_; });
            if (!playback_start_success_)
            {
                lock.unlock();
                if (playback_thread_.joinable())
                    playback_thread_.join();
                return failure("Unable to initialize the Windows audio output device.");
            }
        }

        playback_active_ = true;
        playing_ = true;
        playback_condition_.notify_all();
        return success();
#endif
    }

    MediaBackendResult pause() override
    {
        if (!opened_ || !playing_)
            return failure("The media source is not playing.");
#ifdef _WIN32
        std::unique_lock<std::mutex> lock(playback_mutex_);
        playback_active_ = false;
        playing_ = false;
        playback_condition_.notify_all();
        playback_condition_.wait(lock, [this] { return !playback_writing_; });
#else
        playing_ = false;
#endif
        return success();
    }

    MediaBackendResult seek(int64_t position_ms) override
    {
        if (!opened_ || position_ms < 0)
            return failure("The requested seek position is invalid.");

#ifdef _WIN32
        std::unique_lock<std::mutex> playback_lock(playback_mutex_);
        const bool resume_playback = playback_active_;
        playback_active_ = false;
        playback_condition_.notify_all();
        playback_condition_.wait(playback_lock, [this] { return !playback_writing_; });
        playback_lock.unlock();
#endif

        std::lock_guard<std::mutex> decode_lock(decode_mutex_);
#ifdef _WIN32
        const auto restore_playback = [this, resume_playback, &playback_lock]
        {
            playback_lock.lock();
            if (resume_playback)
                playback_active_ = true;
            playback_condition_.notify_all();
        };
#endif
        const int64_t timestamp = av_rescale_q(
            position_ms,
            AVRational{1, 1000},
            AVRational{1, AV_TIME_BASE});
        const int result = avformat_seek_file(
            format_context_,
            -1,
            std::numeric_limits<int64_t>::min(),
            timestamp,
            std::numeric_limits<int64_t>::max(),
            AVSEEK_FLAG_BACKWARD);
        if (result < 0)
        {
#ifdef _WIN32
            restore_playback();
#endif
            return failure("Unable to seek media source: " + ffmpeg_error(result));
        }

        avformat_flush(format_context_);
        if (audio_decoder_)
            avcodec_flush_buffers(audio_decoder_);
        if (audio_packet_)
            av_packet_unref(audio_packet_);
        audio_packet_pending_ = false;
        audio_draining_ = false;
        if (resampler_)
        {
            swr_close(resampler_);
            const int resampler_result = swr_init(resampler_);
            if (resampler_result < 0)
            {
#ifdef _WIN32
                restore_playback();
#endif
                return failure("Unable to reset audio resampler: " + ffmpeg_error(resampler_result));
            }
        }
#ifdef _WIN32
        restore_playback();
#endif
        return success();
    }

    MediaBackendResult stop() override
    {
        if (!opened_)
            return success();

        if (playing_)
        {
            const MediaBackendResult pause_result = pause();
            if (!pause_result.success)
                return pause_result;
        }

        const MediaBackendResult result = seek(0);
        if (result.success)
            playing_ = false;
        return result;
    }

    MediaBackendResult close() override
    {
#ifdef _WIN32
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_stop_ = true;
            playback_active_ = false;
            playback_condition_.notify_all();
        }
        if (playback_thread_.joinable())
            playback_thread_.join();
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_stop_ = false;
            playback_start_finished_ = false;
            playback_start_success_ = false;
            playback_writing_ = false;
        }
#endif
        std::lock_guard<std::mutex> decode_lock(decode_mutex_);
        swr_free(&resampler_);
        av_frame_free(&audio_frame_);
        av_packet_free(&audio_packet_);
        avcodec_free_context(&audio_decoder_);
        if (format_context_)
            avformat_close_input(&format_context_);

        audio_stream_index_ = -1;
        video_stream_index_ = -1;
        position_ms_.store(0, std::memory_order_relaxed);
        playback_ended_.store(false, std::memory_order_relaxed);
        opened_ = false;
        playing_ = false;
        audio_draining_ = false;
        audio_packet_pending_ = false;
        return success();
    }

    int64_t position_ms() const override
    {
        return position_ms_.load(std::memory_order_relaxed);
    }

    bool playback_ended() const override
    {
        return playback_ended_.load(std::memory_order_relaxed);
    }

    void set_volume(float volume) override
    {
        volume_.store(volume, std::memory_order_relaxed);
    }

    void set_playback_end_callback(std::function<void()> callback) override
    {
        playback_end_callback_ = std::move(callback);
    }

    MediaBackendResult read_audio_frame(AudioFrame& output, bool& end_of_stream) override
    {
        std::lock_guard<std::mutex> decode_lock(decode_mutex_);
        end_of_stream = false;
        output.samples.clear();

        if (!opened_ || !audio_decoder_ || !audio_packet_ || !audio_frame_)
            return failure("No decodable audio stream is open.");

        for (;;)
        {
            int result = avcodec_receive_frame(audio_decoder_, audio_frame_);
            if (result == 0)
                return convert_audio_frame(output);

            if (result == AVERROR_EOF)
            {
                end_of_stream = true;
                return success();
            }

            if (result != AVERROR(EAGAIN))
                return failure("Audio decoding failed: " + ffmpeg_error(result));

            if (audio_draining_)
            {
                result = avcodec_send_packet(audio_decoder_, nullptr);
                if (result == AVERROR_EOF)
                {
                    end_of_stream = true;
                    return success();
                }
                if (result < 0 && result != AVERROR(EAGAIN))
                    return failure("Unable to drain audio decoder: " + ffmpeg_error(result));
                if (result == 0)
                    audio_draining_ = false;
                continue;
            }

            if (audio_packet_pending_)
            {
                result = avcodec_send_packet(audio_decoder_, audio_packet_);
                if (result == AVERROR(EAGAIN))
                    continue;
                if (result < 0)
                    return failure("Unable to submit audio packet: " + ffmpeg_error(result));
                av_packet_unref(audio_packet_);
                audio_packet_pending_ = false;
                continue;
            }

            result = av_read_frame(format_context_, audio_packet_);
            if (result == AVERROR_EOF)
            {
                audio_draining_ = true;
                continue;
            }
            if (result < 0)
                return failure("Unable to read media packet: " + ffmpeg_error(result));

            if (audio_packet_->stream_index != audio_stream_index_)
            {
                av_packet_unref(audio_packet_);
                continue;
            }

            audio_packet_pending_ = true;
        }
    }

private:
#ifdef _WIN32
    void audio_playback_loop()
    {
        WasapiAudioOutput output;
        const bool output_opened = output.open();

        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_start_success_ = output_opened;
            playback_start_finished_ = true;
            if (output_opened)
                output_sample_rate_ = output.sample_rate();
            playback_condition_.notify_all();
        }

        if (!output_opened)
            return;

        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(playback_mutex_);
                playback_condition_.wait(lock, [this]
                {
                    return playback_stop_ || playback_active_;
                });
                if (playback_stop_)
                    break;
            }

            AudioFrame frame;
            bool end_of_stream = false;
            const MediaBackendResult decode_result = read_audio_frame(frame, end_of_stream);
            if (!decode_result.success || end_of_stream)
            {
                {
                    std::lock_guard<std::mutex> lock(playback_mutex_);
                    playback_active_ = false;
                    playing_ = false;
                    if (decode_result.success && end_of_stream)
                        playback_ended_.store(true, std::memory_order_relaxed);
                    playback_condition_.notify_all();
                }

                if (decode_result.success && end_of_stream && playback_end_callback_)
                    playback_end_callback_();
                continue;
            }

            if (frame.samples.empty())
                continue;

            {
                std::lock_guard<std::mutex> lock(playback_mutex_);
                if (!playback_active_ || playback_stop_)
                    continue;
                playback_writing_ = true;
            }

            const float volume = volume_.load(std::memory_order_relaxed);
            if (volume != 1.0f)
            {
                for (float& sample : frame.samples)
                    sample *= volume;
            }

            const bool write_success = output.write(
                frame.samples.data(),
                static_cast<uint32_t>(frame.samples.size() / frame.channels));

            {
                std::lock_guard<std::mutex> lock(playback_mutex_);
                playback_writing_ = false;
                if (!write_success)
                {
                    playback_active_ = false;
                    playing_ = false;
                }
                playback_condition_.notify_all();
            }
        }

        output.close();
    }
#endif

    MediaBackendResult convert_audio_frame(AudioFrame& output)
    {
        AVChannelLayout input_layout{};
        if (audio_frame_->ch_layout.nb_channels > 0)
            av_channel_layout_copy(&input_layout, &audio_frame_->ch_layout);
        else
            av_channel_layout_default(&input_layout, audio_decoder_->ch_layout.nb_channels);

        AVChannelLayout output_layout{};
        av_channel_layout_default(&output_layout, kOutputChannels);

        if (!resampler_)
        {
            const int result = swr_alloc_set_opts2(
                &resampler_,
                &output_layout,
                AV_SAMPLE_FMT_FLT,
                output_sample_rate_,
                &input_layout,
                static_cast<AVSampleFormat>(audio_frame_->format),
                audio_frame_->sample_rate,
                0,
                nullptr);
            if (result < 0)
            {
                av_channel_layout_uninit(&input_layout);
                av_channel_layout_uninit(&output_layout);
                return failure("Unable to configure audio resampling: " + ffmpeg_error(result));
            }

            const int init_result = swr_init(resampler_);
            if (init_result < 0)
            {
                av_channel_layout_uninit(&input_layout);
                av_channel_layout_uninit(&output_layout);
                return failure("Unable to initialize audio resampling: " + ffmpeg_error(init_result));
            }
        }

        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);

        const int capacity = static_cast<int>(av_rescale_rnd(
            swr_get_delay(resampler_, audio_frame_->sample_rate) + audio_frame_->nb_samples,
            output_sample_rate_,
            audio_frame_->sample_rate,
            AV_ROUND_UP));

        output.samples.resize(static_cast<size_t>(capacity) * kOutputChannels);
        uint8_t* destination[] = {
            reinterpret_cast<uint8_t*>(output.samples.data())
        };
        const int converted = swr_convert(
            resampler_,
            destination,
            capacity,
            const_cast<const uint8_t**>(audio_frame_->extended_data),
            audio_frame_->nb_samples);

        if (converted < 0)
        {
            output.samples.clear();
            av_frame_unref(audio_frame_);
            return failure("Audio resampling failed: " + ffmpeg_error(converted));
        }

        output.samples.resize(static_cast<size_t>(converted) * kOutputChannels);
        output.sample_rate = output_sample_rate_;
        output.channels = kOutputChannels;

        const AVStream* stream = format_context_->streams[audio_stream_index_];
        const int64_t timestamp = audio_frame_->best_effort_timestamp;
        output.pts_ms = timestamp == AV_NOPTS_VALUE
            ? position_ms_.load(std::memory_order_relaxed)
            : av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000});
        position_ms_.store(output.pts_ms, std::memory_order_relaxed);

        av_frame_unref(audio_frame_);
        return success();
    }

    static MediaBackendResult success()
    {
        return MediaBackendResult{true, {}};
    }

    static MediaBackendResult failure(std::string message)
    {
        return MediaBackendResult{false, std::move(message)};
    }

    AVFormatContext* format_context_ = nullptr;
    std::mutex decode_mutex_;
#ifdef _WIN32
    std::mutex playback_mutex_;
    std::condition_variable playback_condition_;
    std::thread playback_thread_;
    bool playback_stop_ = false;
    bool playback_active_ = false;
    bool playback_writing_ = false;
    bool playback_start_finished_ = false;
    bool playback_start_success_ = false;
#endif
    int output_sample_rate_ = kOutputSampleRate;
    std::atomic<int64_t> position_ms_{0};
    std::atomic<bool> playback_ended_{false};
    std::atomic<float> volume_{1.0f};
    std::function<void()> playback_end_callback_;
    AVCodecContext* audio_decoder_ = nullptr;
    AVPacket* audio_packet_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    int audio_stream_index_ = -1;
    int video_stream_index_ = -1;
    bool opened_ = false;
    std::atomic<bool> playing_{false};
    bool audio_draining_ = false;
    bool audio_packet_pending_ = false;
};

}

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend()
{
    return std::make_unique<FFmpegMediaBackend>();
}

}
