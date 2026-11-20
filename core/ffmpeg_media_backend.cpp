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
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <d3d11.h>
#endif

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

        start_time_ms_ = format_context_->start_time == AV_NOPTS_VALUE
            ? 0
            : av_rescale_q(
                format_context_->start_time,
                AVRational{1, AV_TIME_BASE},
                AVRational{1, 1000});

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

        if (info.has_video)
        {
            AVStream* stream = format_context_->streams[video_stream_index_];
            const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!decoder)
            {
                close();
                return failure("No FFmpeg decoder is available for the video stream.");
            }

            video_decoder_ = avcodec_alloc_context3(decoder);
            if (!video_decoder_)
            {
                close();
                return failure("Unable to allocate the video decoder.");
            }

            result = avcodec_parameters_to_context(video_decoder_, stream->codecpar);
            if (result < 0)
            {
                const std::string message =
                    "Unable to configure the video decoder: " + ffmpeg_error(result);
                close();
                return failure(message);
            }

#ifdef _WIN32
            if (create_video_hardware_device())
            {
                video_decoder_->hw_device_ctx = av_buffer_ref(hw_device_context_);
                video_decoder_->get_format = select_video_pixel_format;
                video_hardware_decode_ = true;
            }
#endif

            result = avcodec_open2(video_decoder_, decoder, nullptr);
#ifdef _WIN32
            if (result < 0 && video_hardware_decode_)
            {
                avcodec_free_context(&video_decoder_);
                av_buffer_unref(&hw_device_context_);
                video_hardware_decode_ = false;
                video_device_shared_ = false;

                video_decoder_ = avcodec_alloc_context3(decoder);
                if (!video_decoder_)
                {
                    close();
                    return failure("Unable to allocate the software video decoder.");
                }
                result = avcodec_parameters_to_context(video_decoder_, stream->codecpar);
                if (result >= 0)
                    result = avcodec_open2(video_decoder_, decoder, nullptr);
            }
#endif
            if (result < 0)
            {
                const std::string message =
                    "Unable to open the video decoder: " + ffmpeg_error(result);
                close();
                return failure(message);
            }

            video_packet_ = av_packet_alloc();
            video_frame_ = av_frame_alloc();
            if (!video_packet_ || !video_frame_)
            {
                close();
                return failure("Unable to allocate FFmpeg video decode buffers.");
            }
        }

        opened_ = true;
        position_ms_.store(0, std::memory_order_relaxed);
        playback_ended_.store(false, std::memory_order_relaxed);
        playback_failed_.store(false, std::memory_order_relaxed);
        audio_draining_ = false;
        audio_packet_pending_ = false;
        video_draining_ = false;
        video_packet_pending_ = false;
        demux_eof_ = false;
        resampler_drained_ = false;
        clear_packet_queues();
        audio_consumer_active_ = audio_decoder_ != nullptr;
        video_consumer_active_ = false;
        video_consumer_seen_ = false;
        audio_stream_ended_ = false;
        video_stream_ended_ = false;
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
        {
            if (!video_decoder_)
                return failure("The media source has no playable stream.");
            playing_ = true;
            playback_ended_.store(false, std::memory_order_relaxed);
            return success();
        }

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
        if (!opened_)
            return failure("The media source is not playing.");
#ifdef _WIN32
        if (!audio_decoder_)
        {
            if (!playing_)
                return failure("The media source is not playing.");
            playing_ = false;
            return success();
        }

        std::unique_lock<std::mutex> lock(playback_mutex_);
        if (!playing_)
            return failure("The media source is not playing.");
        playback_active_ = false;
        playing_ = false;
        playback_flush_completed_ = false;
        playback_flush_success_ = false;
        playback_flush_requested_ = true;
        playback_condition_.notify_all();
        playback_condition_.wait(lock, [this]
        {
            return !playback_writing_ && playback_flush_completed_;
        });
        if (!playback_flush_success_)
            return failure("Unable to pause the Windows audio output device.");
#else
        if (!playing_)
            return failure("The media source is not playing.");
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
        ++playback_generation_;
        playback_active_ = false;
        playback_condition_.notify_all();
        playback_condition_.wait(playback_lock, [this] { return !playback_writing_; });
        playback_lock.unlock();
#endif

        std::scoped_lock decode_lock(audio_decode_mutex_, video_decode_mutex_);
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
#ifdef _WIN32
        bool seek_succeeded = false;
        const auto restore_playback = [this, resume_playback, &playback_lock, &seek_succeeded]
        {
            playback_lock.lock();
            if (seek_succeeded && audio_decoder_)
            {
                playback_flush_completed_ = false;
                playback_flush_success_ = false;
                playback_flush_requested_ = true;
            }
            if (resume_playback)
                playback_active_ = true;
            playback_condition_.notify_all();
        };
#endif
        const int64_t timestamp = av_rescale_q(
            position_ms + start_time_ms_,
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
        if (video_decoder_)
            avcodec_flush_buffers(video_decoder_);
        if (audio_packet_)
            av_packet_unref(audio_packet_);
        if (video_packet_)
            av_packet_unref(video_packet_);
        audio_packet_pending_ = false;
        audio_draining_ = false;
        video_packet_pending_ = false;
        video_draining_ = false;
        demux_eof_ = false;
        clear_packet_queues_locked();
        audio_consumer_active_ = audio_decoder_ != nullptr;
        video_consumer_active_ = video_decoder_ != nullptr && video_consumer_seen_;
        audio_stream_ended_ = false;
        video_stream_ended_ = false;
        resampler_drained_ = false;
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

        position_ms_.store(position_ms, std::memory_order_relaxed);
#ifdef _WIN32
        audio_clock_valid_.store(false, std::memory_order_relaxed);
#endif
        playback_ended_.store(false, std::memory_order_relaxed);
        playback_failed_.store(false, std::memory_order_relaxed);
#ifdef _WIN32
        seek_succeeded = true;
        restore_playback();
#endif
        return success();
    }

    MediaBackendResult stop() override
    {
        if (!opened_)
            return success();

#ifdef _WIN32
        bool is_playing = false;
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            is_playing = playing_;
        }
#else
        const bool is_playing = playing_;
#endif
        if (is_playing)
        {
            const MediaBackendResult pause_result = pause();
            if (!pause_result.success)
                return pause_result;
        }

        const MediaBackendResult result = seek(0);
        if (result.success)
        {
#ifdef _WIN32
            std::lock_guard<std::mutex> lock(playback_mutex_);
#endif
            playing_ = false;
        }
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
            playback_flush_requested_ = false;
        }
#endif
        std::scoped_lock decode_lock(audio_decode_mutex_, video_decode_mutex_);
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
        swr_free(&resampler_);
        sws_freeContext(video_scaler_);
        video_scaler_ = nullptr;
        av_frame_free(&transfer_frame_);
        av_frame_free(&video_frame_);
        av_packet_free(&video_packet_);
        avcodec_free_context(&video_decoder_);
        av_buffer_unref(&hw_device_context_);
        video_hardware_decode_ = false;
        video_device_shared_ = false;
        av_frame_free(&audio_frame_);
        av_packet_free(&audio_packet_);
        avcodec_free_context(&audio_decoder_);
        clear_packet_queues_locked();
        if (format_context_)
            avformat_close_input(&format_context_);

        audio_stream_index_ = -1;
        video_stream_index_ = -1;
        start_time_ms_ = 0;
        position_ms_.store(0, std::memory_order_relaxed);
#ifdef _WIN32
        audio_clock_valid_.store(false, std::memory_order_relaxed);
#endif
        playback_ended_.store(false, std::memory_order_relaxed);
        playback_failed_.store(false, std::memory_order_relaxed);
        opened_ = false;
        playing_ = false;
        audio_draining_ = false;
        audio_packet_pending_ = false;
        video_draining_ = false;
        video_packet_pending_ = false;
        demux_eof_ = false;
        resampler_drained_ = false;
        audio_consumer_active_ = false;
        video_consumer_active_ = false;
        video_consumer_seen_ = false;
        audio_stream_ended_ = false;
        video_stream_ended_ = false;
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

    void set_playback_rate(float rate) override
    {
        std::lock_guard<std::mutex> lock(audio_decode_mutex_);
        playback_rate_.store(rate, std::memory_order_relaxed);
#ifdef _WIN32
        audio_clock_valid_.store(false, std::memory_order_relaxed);
#endif
        swr_free(&resampler_);
        resampler_drained_ = false;
    }

    void set_playback_end_callback(std::function<void()> callback) override
    {
        playback_end_callback_ = std::move(callback);
    }

    bool playback_failed() const override
    {
        return playback_failed_.load(std::memory_order_relaxed);
    }

    void set_playback_error_callback(std::function<void(std::string)> callback) override
    {
        playback_error_callback_ = std::move(callback);
    }

    MediaBackendResult read_audio_frame(AudioFrame& output, bool& end_of_stream) override
    {
        std::lock_guard<std::mutex> decode_lock(audio_decode_mutex_);
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
                if (resampler_ && !resampler_drained_)
                {
                    const int target_sample_rate = static_cast<int>(std::lround(
                        output_sample_rate_ / playback_rate_.load(std::memory_order_relaxed)));
                    const int capacity = static_cast<int>(
                        swr_get_delay(resampler_, target_sample_rate));
                    if (capacity > 0)
                    {
                        output.samples.resize(
                            static_cast<size_t>(capacity) * kOutputChannels);
                        uint8_t* destination[] = {
                            reinterpret_cast<uint8_t*>(output.samples.data())
                        };
                        const int converted = swr_convert(
                            resampler_, destination, capacity, nullptr, 0);
                        if (converted < 0)
                        {
                            output.samples.clear();
                            return failure(
                                "Unable to drain audio resampler: " + ffmpeg_error(converted));
                        }
                        output.samples.resize(
                            static_cast<size_t>(converted) * kOutputChannels);
                        if (converted > 0)
                        {
                            output.sample_rate = target_sample_rate;
                            output.channels = kOutputChannels;
                            output.pts_ms = position_ms_.load(std::memory_order_relaxed);
                            return success();
                        }
                    }
                    resampler_drained_ = true;
                }

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
                    if (!audio_decoder_)
                    {
                        playback_ended_.store(true, std::memory_order_relaxed);
                        playing_ = false;
                        if (playback_end_callback_)
                            playback_end_callback_();
                    }
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

            result = next_packet_for_stream(audio_stream_index_, audio_packet_);
            if (result == AVERROR_EOF)
            {
                audio_draining_ = true;
                continue;
            }
            if (result == AVERROR(EAGAIN))
                return success();
            if (result < 0)
                return failure("Unable to read media packet: " + ffmpeg_error(result));

            audio_packet_pending_ = true;
        }
    }

    MediaBackendResult read_video_frame(VideoFrame& output, bool& end_of_stream) override
    {
        std::lock_guard<std::mutex> decode_lock(video_decode_mutex_);
        end_of_stream = false;
        output.bgra.clear();

        if (!opened_ || !video_decoder_ || !video_packet_ || !video_frame_)
            return failure("No decodable video stream is open.");
        {
            std::lock_guard<std::mutex> demux_lock(demux_mutex_);
            video_consumer_active_ = true;
            video_consumer_seen_ = true;
        }

        for (;;)
        {
            int result = avcodec_receive_frame(video_decoder_, video_frame_);
            if (result == 0)
                return convert_video_frame(output);

            if (result == AVERROR_EOF)
            {
                end_of_stream = true;
                if (mark_stream_ended(false) && playback_end_callback_)
                    playback_end_callback_();
                return success();
            }

            if (result != AVERROR(EAGAIN))
                return failure("Video decoding failed: " + ffmpeg_error(result));

            if (video_draining_)
            {
                result = avcodec_send_packet(video_decoder_, nullptr);
                if (result == AVERROR_EOF)
                {
                    end_of_stream = true;
                    if (mark_stream_ended(false) && playback_end_callback_)
                        playback_end_callback_();
                    return success();
                }
                if (result < 0 && result != AVERROR(EAGAIN))
                    return failure("Unable to drain video decoder: " + ffmpeg_error(result));
                if (result == 0)
                    video_draining_ = false;
                continue;
            }

            if (video_packet_pending_)
            {
                result = avcodec_send_packet(video_decoder_, video_packet_);
                if (result == AVERROR(EAGAIN))
                    continue;
                if (result < 0)
                    return failure("Unable to submit video packet: " + ffmpeg_error(result));
                av_packet_unref(video_packet_);
                video_packet_pending_ = false;
                continue;
            }

            result = next_packet_for_stream(video_stream_index_, video_packet_);
            if (result == AVERROR_EOF)
            {
                video_draining_ = true;
                continue;
            }
            if (result == AVERROR(EAGAIN))
                return success();
            if (result < 0)
                return failure("Unable to read media packet: " + ffmpeg_error(result));

            video_packet_pending_ = true;
        }
    }

private:
    bool mark_stream_ended(bool audio_stream)
    {
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
        if (audio_stream)
        {
            audio_stream_ended_ = true;
            audio_consumer_active_ = false;
        }
        else
        {
            video_stream_ended_ = true;
            video_consumer_active_ = false;
        }

        const bool all_audio_ended = !audio_decoder_ || audio_stream_ended_;
        const bool all_video_ended =
            !video_decoder_ || !video_consumer_seen_ || video_stream_ended_;
        if (!all_audio_ended || !all_video_ended)
            return false;

        bool expected = false;
        if (!playback_ended_.compare_exchange_strong(expected, true, std::memory_order_relaxed))
            return false;

        playing_ = false;
        return true;
    }

    int next_packet_for_stream(int stream_index, AVPacket* output_packet)
    {
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
        std::deque<AVPacket*>& queue =
            stream_index == audio_stream_index_ ? audio_packet_queue_ : video_packet_queue_;

        if (!queue.empty())
        {
            AVPacket* queued = queue.front();
            queue.pop_front();
            size_t& queued_bytes = stream_index == audio_stream_index_
                ? audio_packet_queue_bytes_
                : video_packet_queue_bytes_;
            queued_bytes -= static_cast<size_t>(std::max(queued->size, 0));
            av_packet_move_ref(output_packet, queued);
            av_packet_free(&queued);
            return 0;
        }

        if (deferred_packet_)
        {
            if (deferred_packet_->stream_index == stream_index)
            {
                av_packet_move_ref(output_packet, deferred_packet_);
                av_packet_free(&deferred_packet_);
                return 0;
            }

            std::deque<AVPacket*>* destination = nullptr;
            if (deferred_packet_->stream_index == audio_stream_index_ && audio_consumer_active_)
                destination = &audio_packet_queue_;
            else if (deferred_packet_->stream_index == video_stream_index_ && video_consumer_active_)
                destination = &video_packet_queue_;

            if (destination)
            {
                constexpr size_t kMaximumQueuedPackets = 512;
                constexpr size_t kMaximumQueuedBytes = 64 * 1024 * 1024;
                size_t& queued_bytes = deferred_packet_->stream_index == audio_stream_index_
                    ? audio_packet_queue_bytes_
                    : video_packet_queue_bytes_;
                const size_t packet_bytes = static_cast<size_t>(
                    std::max(deferred_packet_->size, 0));
                if (destination->size() >= kMaximumQueuedPackets ||
                    queued_bytes + packet_bytes > kMaximumQueuedBytes)
                    return AVERROR(EAGAIN);
                destination->push_back(deferred_packet_);
                queued_bytes += packet_bytes;
                deferred_packet_ = nullptr;
            }
            else
            {
                av_packet_free(&deferred_packet_);
            }
        }

        for (;;)
        {
            AVPacket* packet = av_packet_alloc();
            const int result = av_read_frame(format_context_, packet);
            if (result < 0)
            {
                av_packet_free(&packet);
                if (result == AVERROR_EOF)
                    demux_eof_ = true;
                return result;
            }

            if (packet->stream_index == stream_index)
            {
                av_packet_move_ref(output_packet, packet);
                av_packet_free(&packet);
                return 0;
            }

            std::deque<AVPacket*>* destination = nullptr;
            if (packet->stream_index == audio_stream_index_ && audio_consumer_active_)
                destination = &audio_packet_queue_;
            else if (packet->stream_index == video_stream_index_ && video_consumer_active_)
                destination = &video_packet_queue_;

            if (destination)
            {
                constexpr size_t kMaximumQueuedPackets = 512;
                constexpr size_t kMaximumQueuedBytes = 64 * 1024 * 1024;
                size_t& queued_bytes = packet->stream_index == audio_stream_index_
                    ? audio_packet_queue_bytes_
                    : video_packet_queue_bytes_;
                const size_t packet_bytes = static_cast<size_t>(
                    std::max(packet->size, 0));
                if (destination->size() >= kMaximumQueuedPackets ||
                    queued_bytes + packet_bytes > kMaximumQueuedBytes)
                {
                    deferred_packet_ = packet;
                    return AVERROR(EAGAIN);
                }
                destination->push_back(packet);
                queued_bytes += packet_bytes;
            }
            else
            {
                av_packet_free(&packet);
            }

            if (demux_eof_)
                return AVERROR_EOF;
        }
    }

    void clear_packet_queues()
    {
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
        clear_packet_queues_locked();
    }

    void clear_packet_queues_locked()
    {
        for (AVPacket* packet : audio_packet_queue_)
            av_packet_free(&packet);
        for (AVPacket* packet : video_packet_queue_)
            av_packet_free(&packet);
        av_packet_free(&deferred_packet_);
        audio_packet_queue_.clear();
        video_packet_queue_.clear();
        audio_packet_queue_bytes_ = 0;
        video_packet_queue_bytes_ = 0;
    }

    MediaBackendResult convert_video_frame(VideoFrame& output)
    {
        AVFrame* source_frame = video_frame_;
#ifdef _WIN32
        if (video_frame_->format == AV_PIX_FMT_D3D11 && video_device_shared_)
        {
            AVFrame* retained_frame = av_frame_clone(video_frame_);
            if (!retained_frame)
            {
                av_frame_unref(video_frame_);
                return failure("Unable to retain the D3D11 video frame.");
            }

            ID3D11Texture2D* texture =
                reinterpret_cast<ID3D11Texture2D*>(retained_frame->data[0]);
            if (!texture)
            {
                av_frame_free(&retained_frame);
                av_frame_unref(video_frame_);
                return failure("The D3D11 video frame has no texture.");
            }

            D3D11_TEXTURE2D_DESC texture_desc{};
            texture->GetDesc(&texture_desc);
            output.width = retained_frame->width;
            output.height = retained_frame->height;
            output.d3d11_texture = texture;
            output.d3d11_array_slice = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(retained_frame->data[1]));
            output.dxgi_format = static_cast<uint32_t>(texture_desc.Format);
            output.hardware_frame_owner = std::shared_ptr<void>(
                retained_frame,
                [](void* frame)
                {
                    AVFrame* owned_frame = static_cast<AVFrame*>(frame);
                    av_frame_free(&owned_frame);
                });

            const AVStream* stream = format_context_->streams[video_stream_index_];
            AVRational sample_aspect_ratio = retained_frame->sample_aspect_ratio;
            if (sample_aspect_ratio.num <= 0 || sample_aspect_ratio.den <= 0)
                sample_aspect_ratio = stream->sample_aspect_ratio;
            output.display_aspect_ratio =
                sample_aspect_ratio.num > 0 && sample_aspect_ratio.den > 0
                    ? static_cast<float>(output.width) * sample_aspect_ratio.num /
                        (static_cast<float>(output.height) * sample_aspect_ratio.den)
                    : static_cast<float>(output.width) / static_cast<float>(output.height);
            const int64_t timestamp = retained_frame->best_effort_timestamp;
            output.pts_ms = timestamp == AV_NOPTS_VALUE
                ? position_ms_.load(std::memory_order_relaxed)
                : av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000}) -
                    start_time_ms_;
            if (!audio_decoder_)
                position_ms_.store(output.pts_ms, std::memory_order_relaxed);

            av_frame_unref(video_frame_);
            return success();
        }
#endif
#ifdef _WIN32
        if (video_frame_->format == AV_PIX_FMT_D3D11)
        {
            if (!transfer_frame_)
                transfer_frame_ = av_frame_alloc();
            if (!transfer_frame_)
            {
                av_frame_unref(video_frame_);
                return failure("Unable to allocate a D3D11 video transfer frame.");
            }

            av_frame_unref(transfer_frame_);
            const int transfer_result = av_hwframe_transfer_data(
                transfer_frame_, video_frame_, 0);
            if (transfer_result < 0)
            {
                av_frame_unref(transfer_frame_);
                av_frame_unref(video_frame_);
                return failure("Unable to transfer a D3D11 video frame: " +
                    ffmpeg_error(transfer_result));
            }
            source_frame = transfer_frame_;
        }
#endif
        const int width = source_frame->width;
        const int height = source_frame->height;
        if (width <= 0 || height <= 0)
        {
            if (source_frame != video_frame_)
                av_frame_unref(transfer_frame_);
            av_frame_unref(video_frame_);
            return failure("Decoded video frame has invalid dimensions.");
        }

        video_scaler_ = sws_getCachedContext(
            video_scaler_,
            width,
            height,
            static_cast<AVPixelFormat>(source_frame->format),
            width,
            height,
            AV_PIX_FMT_BGRA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (!video_scaler_)
        {
            if (source_frame != video_frame_)
                av_frame_unref(transfer_frame_);
            av_frame_unref(video_frame_);
            return failure("Unable to initialize video pixel format conversion.");
        }

        output.width = width;
        output.height = height;
        output.stride = width * 4;
        output.bgra.resize(static_cast<size_t>(output.stride) * height);
        uint8_t* destination[] = {output.bgra.data()};
        int destination_linesize[] = {output.stride};
        const int rows = sws_scale(
            video_scaler_,
            source_frame->data,
            source_frame->linesize,
            0,
            height,
            destination,
            destination_linesize);
        if (rows != height)
        {
            output.bgra.clear();
            if (source_frame != video_frame_)
                av_frame_unref(transfer_frame_);
            av_frame_unref(video_frame_);
            return failure("Unable to convert decoded video frame.");
        }

        const AVStream* stream = format_context_->streams[video_stream_index_];
        AVRational sample_aspect_ratio = video_frame_->sample_aspect_ratio;
        if (sample_aspect_ratio.num <= 0 || sample_aspect_ratio.den <= 0)
            sample_aspect_ratio = stream->sample_aspect_ratio;
        output.display_aspect_ratio =
            sample_aspect_ratio.num > 0 && sample_aspect_ratio.den > 0
                ? static_cast<float>(width) * sample_aspect_ratio.num /
                    (static_cast<float>(height) * sample_aspect_ratio.den)
                : static_cast<float>(width) / static_cast<float>(height);
        const int64_t timestamp = video_frame_->best_effort_timestamp;
        output.pts_ms = timestamp == AV_NOPTS_VALUE
            ? position_ms_.load(std::memory_order_relaxed)
            : av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000}) -
                start_time_ms_;
        if (!audio_decoder_)
            position_ms_.store(output.pts_ms, std::memory_order_relaxed);
        if (source_frame != video_frame_)
            av_frame_unref(transfer_frame_);
        av_frame_unref(video_frame_);
        return success();
    }

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
            uint64_t decode_generation = 0;
            {
                std::unique_lock<std::mutex> lock(playback_mutex_);
                playback_condition_.wait(lock, [this]
                {
                    return playback_stop_ || playback_active_ || playback_flush_requested_;
                });
                if (playback_stop_)
                    break;
                if (playback_flush_requested_)
                {
                    playback_flush_requested_ = false;
                    lock.unlock();
                    const bool flush_success = output.flush();
                    audio_clock_valid_.store(false, std::memory_order_relaxed);
                    {
                        std::lock_guard<std::mutex> state_lock(playback_mutex_);
                        playback_flush_success_ = flush_success;
                        playback_flush_completed_ = true;
                        if (!flush_success)
                        {
                            playback_active_ = false;
                            playing_ = false;
                            playback_failed_.store(true, std::memory_order_relaxed);
                        }
                        playback_condition_.notify_all();
                    }
                    if (!flush_success && playback_error_callback_)
                        playback_error_callback_("WASAPI failed to flush its render buffer.");
                    continue;
                }
                decode_generation = playback_generation_;
            }

            AudioFrame frame;
            bool end_of_stream = false;
            const MediaBackendResult decode_result = read_audio_frame(frame, end_of_stream);
            if (!decode_result.success || end_of_stream)
            {
                bool drain_success = true;
                bool drain_audio = false;
                if (decode_result.success && end_of_stream)
                {
                    {
                        std::lock_guard<std::mutex> lock(playback_mutex_);
                        drain_audio = decode_generation == playback_generation_ && playback_active_;
                    }
                    if (drain_audio)
                        drain_success = output.drain();
                }

                bool stale_result = false;
                {
                    std::lock_guard<std::mutex> lock(playback_mutex_);
                    stale_result = decode_generation != playback_generation_ ||
                        (decode_result.success && end_of_stream && !playback_active_);
                    if (!stale_result)
                    {
                        playback_active_ = false;
                        if (!decode_result.success || !drain_success)
                        {
                            playing_ = false;
                            playback_failed_.store(true, std::memory_order_relaxed);
                        }
                    }
                    playback_condition_.notify_all();
                }

                if (stale_result)
                    continue;
                const bool overall_ended =
                    decode_result.success && end_of_stream && drain_success
                        ? mark_stream_ended(true)
                        : false;
                if (overall_ended && playback_end_callback_)
                    playback_end_callback_();
                else if ((!decode_result.success || !drain_success) && playback_error_callback_)
                    playback_error_callback_(
                        decode_result.success
                            ? "Unable to drain the Windows audio output buffer."
                            : decode_result.message);
                continue;
            }

            if (frame.samples.empty())
            {
                std::this_thread::yield();
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(playback_mutex_);
                if (!playback_active_ ||
                    playback_stop_ ||
                    decode_generation != playback_generation_)
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
                    playback_failed_.store(true, std::memory_order_relaxed);
                }
                playback_condition_.notify_all();
            }

            if (!write_success && playback_error_callback_)
                playback_error_callback_("Windows audio output failed while writing PCM samples.");

            if (write_success)
            {
                uint32_t queued_frames = 0;
                const int64_t media_duration_ms = static_cast<int64_t>(
                    frame.samples.size() / frame.channels * 1000.0 / frame.sample_rate);
                uint64_t clock_frames = 0;
                const bool has_clock = output.clock_position(clock_frames);
                const bool has_padding = output.current_padding(queued_frames);
                if (has_clock && output_sample_rate_ > 0)
                {
                    if (!audio_clock_valid_.load(std::memory_order_relaxed))
                    {
                        if (!has_padding)
                            continue;
                        audio_clock_anchor_frames_.store(
                            clock_frames + queued_frames, std::memory_order_relaxed);
                        audio_clock_anchor_media_ms_.store(
                            frame.pts_ms + media_duration_ms, std::memory_order_relaxed);
                        audio_clock_valid_.store(true, std::memory_order_relaxed);
                    }

                    const int64_t clock_delta_frames =
                        static_cast<int64_t>(clock_frames) -
                        static_cast<int64_t>(
                            audio_clock_anchor_frames_.load(std::memory_order_relaxed));
                    const double media_delta_ms =
                        clock_delta_frames * 1000.0 *
                        playback_rate_.load(std::memory_order_relaxed) /
                        output_sample_rate_;
                    position_ms_.store(
                        audio_clock_anchor_media_ms_.load(std::memory_order_relaxed) +
                            static_cast<int64_t>(media_delta_ms),
                        std::memory_order_relaxed);
                }
                else if (has_padding && output_sample_rate_ > 0)
                {
                    const int64_t queued_media_ms = static_cast<int64_t>(
                        queued_frames * 1000.0 * playback_rate_.load(
                            std::memory_order_relaxed) / output_sample_rate_);
                    position_ms_.store(
                        frame.pts_ms + media_duration_ms - queued_media_ms,
                        std::memory_order_relaxed);
                    audio_clock_valid_.store(false, std::memory_order_relaxed);
                }
            }
        }

        output.close();
    }
#endif

    MediaBackendResult convert_audio_frame(AudioFrame& output)
    {
        const int target_sample_rate = static_cast<int>(std::lround(
            output_sample_rate_ / playback_rate_.load(std::memory_order_relaxed)));

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
                target_sample_rate,
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
            target_sample_rate,
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
        output.sample_rate = target_sample_rate;
        output.channels = kOutputChannels;

        const AVStream* stream = format_context_->streams[audio_stream_index_];
        const int64_t timestamp = audio_frame_->best_effort_timestamp;
        output.pts_ms = timestamp == AV_NOPTS_VALUE
            ? position_ms_.load(std::memory_order_relaxed)
            : av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000}) -
                start_time_ms_;
        position_ms_.store(output.pts_ms, std::memory_order_relaxed);

        av_frame_unref(audio_frame_);
        return success();
    }

#ifdef _WIN32
    void set_video_device(void* device) override
    {
        std::lock_guard<std::mutex> lock(video_decode_mutex_);
        video_device_ = static_cast<ID3D11Device*>(device);
    }

    bool create_video_hardware_device()
    {
        video_device_shared_ = false;
        if (video_device_)
        {
            hw_device_context_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
            if (hw_device_context_)
            {
                auto* device_context = static_cast<AVD3D11VADeviceContext*>(
                    reinterpret_cast<AVHWDeviceContext*>(hw_device_context_->data)->hwctx);
                device_context->device = video_device_;
                device_context->device->AddRef();
                device_context->device->GetImmediateContext(&device_context->device_context);
                if (av_hwdevice_ctx_init(hw_device_context_) >= 0)
                {
                    video_device_shared_ = true;
                    return true;
                }
                av_buffer_unref(&hw_device_context_);
            }
        }

        return false;
    }

    static AVPixelFormat select_video_pixel_format(
        AVCodecContext*,
        const AVPixelFormat* formats)
    {
        for (const AVPixelFormat* format = formats;
             *format != AV_PIX_FMT_NONE;
             ++format)
        {
            if (*format == AV_PIX_FMT_D3D11)
                return *format;
        }
        return formats[0];
    }
#endif

    static MediaBackendResult success()
    {
        return MediaBackendResult{true, {}};
    }

    static MediaBackendResult failure(std::string message)
    {
        return MediaBackendResult{false, std::move(message)};
    }

    AVFormatContext* format_context_ = nullptr;
    std::mutex audio_decode_mutex_;
    std::mutex video_decode_mutex_;
    std::mutex demux_mutex_;
#ifdef _WIN32
    std::mutex playback_mutex_;
    std::condition_variable playback_condition_;
    std::thread playback_thread_;
    bool playback_stop_ = false;
    bool playback_active_ = false;
    bool playback_writing_ = false;
    bool playback_flush_requested_ = false;
    bool playback_flush_completed_ = false;
    bool playback_flush_success_ = false;
    uint64_t playback_generation_ = 0;
    bool playback_start_finished_ = false;
    bool playback_start_success_ = false;
#endif
    int output_sample_rate_ = kOutputSampleRate;
    std::atomic<int64_t> position_ms_{0};
#ifdef _WIN32
    std::atomic<uint64_t> audio_clock_anchor_frames_{0};
    std::atomic<int64_t> audio_clock_anchor_media_ms_{0};
    std::atomic<bool> audio_clock_valid_{false};
#endif
    std::atomic<bool> playback_ended_{false};
    std::atomic<bool> playback_failed_{false};
#ifdef _WIN32
    ID3D11Device* video_device_ = nullptr;
#endif
    std::atomic<float> volume_{1.0f};
    std::atomic<float> playback_rate_{1.0f};
    std::function<void()> playback_end_callback_;
    std::function<void(std::string)> playback_error_callback_;
    AVCodecContext* audio_decoder_ = nullptr;
    AVPacket* audio_packet_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    AVCodecContext* video_decoder_ = nullptr;
    AVPacket* video_packet_ = nullptr;
    AVFrame* video_frame_ = nullptr;
    AVFrame* transfer_frame_ = nullptr;
    SwsContext* video_scaler_ = nullptr;
    AVBufferRef* hw_device_context_ = nullptr;
    bool video_hardware_decode_ = false;
    bool video_device_shared_ = false;
    std::deque<AVPacket*> audio_packet_queue_;
    std::deque<AVPacket*> video_packet_queue_;
    AVPacket* deferred_packet_ = nullptr;
    size_t audio_packet_queue_bytes_ = 0;
    size_t video_packet_queue_bytes_ = 0;
    int audio_stream_index_ = -1;
    int video_stream_index_ = -1;
    int64_t start_time_ms_ = 0;
    bool opened_ = false;
    std::atomic<bool> playing_{false};
    bool audio_draining_ = false;
    bool audio_packet_pending_ = false;
    bool resampler_drained_ = false;
    bool video_draining_ = false;
    bool video_packet_pending_ = false;
    bool demux_eof_ = false;
    bool audio_consumer_active_ = false;
    bool video_consumer_active_ = false;
    bool video_consumer_seen_ = false;
    bool audio_stream_ended_ = false;
    bool video_stream_ended_ = false;
};

}

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend()
{
    return std::make_unique<FFmpegMediaBackend>();
}

bool convert_d3d11_frame_to_bgra(
    const std::shared_ptr<void>& frame_owner,
    std::vector<uint8_t>& pixels,
    int& width,
    int& height,
    int& stride,
    float& display_aspect_ratio,
    std::string& error)
{
#ifdef _WIN32
    auto* hardware_frame = static_cast<AVFrame*>(frame_owner.get());
    if (!hardware_frame || hardware_frame->format != AV_PIX_FMT_D3D11)
    {
        error = "The retained frame is not a D3D11 hardware frame.";
        return false;
    }

    AVFrame* software_frame = av_frame_alloc();
    if (!software_frame)
    {
        error = "Unable to allocate a software video frame.";
        return false;
    }

    const int transfer_result = av_hwframe_transfer_data(
        software_frame, hardware_frame, 0);
    if (transfer_result < 0)
    {
        error = "Unable to download the D3D11 video frame: " +
            ffmpeg_error(transfer_result);
        av_frame_free(&software_frame);
        return false;
    }

    SwsContext* scaler = sws_getContext(
        software_frame->width,
        software_frame->height,
        static_cast<AVPixelFormat>(software_frame->format),
        software_frame->width,
        software_frame->height,
        AV_PIX_FMT_BGRA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!scaler)
    {
        error = "Unable to initialize hardware-frame BGRA conversion.";
        av_frame_free(&software_frame);
        return false;
    }

    width = software_frame->width;
    height = software_frame->height;
    stride = width * 4;
    pixels.resize(static_cast<size_t>(stride) * height);
    uint8_t* destination[] = {pixels.data()};
    int destination_stride[] = {stride};
    const int rows = sws_scale(
        scaler,
        software_frame->data,
        software_frame->linesize,
        0,
        height,
        destination,
        destination_stride);
    sws_freeContext(scaler);
    av_frame_free(&software_frame);
    if (rows != height)
    {
        pixels.clear();
        error = "Unable to convert the downloaded video frame.";
        return false;
    }

    AVRational sample_aspect_ratio = hardware_frame->sample_aspect_ratio;
    if (sample_aspect_ratio.num > 0 && sample_aspect_ratio.den > 0)
        display_aspect_ratio =
            static_cast<float>(width) * sample_aspect_ratio.num /
            (static_cast<float>(height) * sample_aspect_ratio.den);
    else
        display_aspect_ratio = static_cast<float>(width) / height;
    return true;
#else
    (void)frame_owner;
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    (void)display_aspect_ratio;
    error = "D3D11 frame conversion is only available on Windows.";
    return false;
#endif
}

}
