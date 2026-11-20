#include "ffmpeg_media_backend.h"
#include "subtitle_filter.h"

#ifdef _WIN32
#include "wasapi_audio_output.h"
#endif

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
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
        stream_rotation_degrees_ = video_stream_index_ >= 0
            ? get_stream_rotation_degrees(format_context_->streams[video_stream_index_])
            : 0;

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

        source_path_ = source;

        if (info.has_video)
        {
            int first_subtitle_stream = -1;
            int default_subtitle_stream = -1;
            for (unsigned int index = 0; index < format_context_->nb_streams; ++index)
            {
                AVStream* stream = format_context_->streams[index];
                if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
                    continue;
                if (first_subtitle_stream < 0)
                    first_subtitle_stream = static_cast<int>(index);
                if ((stream->disposition & AV_DISPOSITION_DEFAULT) != 0)
                {
                    default_subtitle_stream = static_cast<int>(index);
                    break;
                }
            }

            const int preferred_stream = default_subtitle_stream >= 0
                ? default_subtitle_stream
                : first_subtitle_stream;
            if (preferred_stream >= 0 &&
                create_subtitle_decoder(preferred_stream, &subtitle_decoder_))
            {
                subtitle_stream_index_ = preferred_stream;
                configure_ass_subtitle_filter(preferred_stream);
            }

            if (!subtitle_decoder_)
            {
                for (unsigned int index = 0; index < format_context_->nb_streams; ++index)
                {
                    AVStream* stream = format_context_->streams[index];
                    if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
                        static_cast<int>(index) == preferred_stream)
                        continue;
                    if (create_subtitle_decoder(static_cast<int>(index), &subtitle_decoder_))
                    {
                        subtitle_stream_index_ = static_cast<int>(index);
                        configure_ass_subtitle_filter(static_cast<int>(index));
                        break;
                    }
                }
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
            {
                std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
                video_clock_anchor_position_ms_ = position_ms_.load(std::memory_order_relaxed);
                video_clock_anchor_time_ = std::chrono::steady_clock::now();
                video_clock_running_ = true;
            }
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
            {
                std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
                position_ms_.store(video_clock_position_locked(), std::memory_order_relaxed);
                video_clock_running_ = false;
            }
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
        if (subtitle_decoder_)
            avcodec_flush_buffers(subtitle_decoder_);
        active_subtitle_cues_.clear();
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
        if (!audio_decoder_)
        {
            std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
            video_clock_anchor_position_ms_ = position_ms;
            video_clock_anchor_time_ = std::chrono::steady_clock::now();
            video_clock_running_ = playing_.load(std::memory_order_relaxed);
        }
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
        avcodec_free_context(&subtitle_decoder_);
        subtitle_filter_.reset();
        subtitle_filter_active_ = false;
        subtitle_filter_path_.clear();
        subtitle_filter_stream_ordinal_ = -1;
        subtitle_filter_width_ = 0;
        subtitle_filter_height_ = 0;
        source_path_.clear();
        active_subtitle_cues_.clear();
        subtitle_stream_index_ = -1;
        clear_packet_queues_locked();
        if (format_context_)
            avformat_close_input(&format_context_);

        audio_stream_index_ = -1;
        video_stream_index_ = -1;
        subtitle_stream_index_ = -1;
        stream_rotation_degrees_ = 0;
        start_time_ms_ = 0;
        position_ms_.store(0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
            video_clock_anchor_position_ms_ = 0;
            video_clock_anchor_time_ = std::chrono::steady_clock::now();
            video_clock_running_ = false;
        }
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
        if (!audio_decoder_)
        {
            std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
            return video_clock_position_locked();
        }
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
        if (!audio_decoder_)
        {
            std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
            video_clock_anchor_position_ms_ = video_clock_position_locked();
            video_clock_anchor_time_ = std::chrono::steady_clock::now();
        }
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

    std::vector<MediaTrackInfo> audio_tracks() const override
    {
        std::vector<MediaTrackInfo> tracks;
        if (!format_context_)
            return tracks;

        for (unsigned int index = 0; index < format_context_->nb_streams; ++index)
        {
            const AVStream* stream = format_context_->streams[index];
            if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;

            MediaTrackInfo track;
            track.stream_index = static_cast<int>(index);
            track.codec = avcodec_get_name(stream->codecpar->codec_id);
            track.channels = stream->codecpar->ch_layout.nb_channels;
            track.sample_rate = stream->codecpar->sample_rate;
            track.is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            track.is_selected = static_cast<int>(index) == audio_stream_index_;

            AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
            AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
            if (title)
                track.title = title->value;
            if (language)
                track.language = language->value;
            tracks.push_back(std::move(track));
        }
        return tracks;
    }

    MediaBackendResult select_audio_track(int stream_index) override
    {
        if (!opened_ || !format_context_)
            return failure("No media source is open.");
        if (playing_.load(std::memory_order_relaxed))
            return failure("Pause playback before switching the audio track.");

        AVStream* selected_stream = nullptr;
        for (unsigned int index = 0; index < format_context_->nb_streams; ++index)
        {
            AVStream* stream = format_context_->streams[index];
            if (static_cast<int>(index) == stream_index &&
                stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                selected_stream = stream;
                break;
            }
        }
        if (!selected_stream)
            return failure("The requested audio track does not exist.");
        if (stream_index == audio_stream_index_)
            return success();

        const AVCodec* decoder = avcodec_find_decoder(selected_stream->codecpar->codec_id);
        if (!decoder)
            return failure("No FFmpeg decoder is available for the selected audio track.");

        AVCodecContext* replacement_decoder = avcodec_alloc_context3(decoder);
        if (!replacement_decoder)
            return failure("Unable to allocate the selected audio decoder.");

        int result = avcodec_parameters_to_context(
            replacement_decoder, selected_stream->codecpar);
        if (result >= 0)
            result = avcodec_open2(replacement_decoder, decoder, nullptr);
        if (result < 0)
        {
            avcodec_free_context(&replacement_decoder);
            return failure("Unable to open the selected audio decoder: " + ffmpeg_error(result));
        }

        const int64_t resume_position = position_ms();
        const MediaBackendResult seek_result = seek(resume_position);
        if (!seek_result.success)
        {
            avcodec_free_context(&replacement_decoder);
            return seek_result;
        }

        {
            std::lock_guard<std::mutex> decode_lock(audio_decode_mutex_);
            std::lock_guard<std::mutex> demux_lock(demux_mutex_);
            avcodec_free_context(&audio_decoder_);
            audio_decoder_ = replacement_decoder;
            audio_stream_index_ = stream_index;
            audio_consumer_active_ = true;
            audio_stream_ended_ = false;
            audio_draining_ = false;
            audio_packet_pending_ = false;
            resampler_drained_ = false;
            swr_free(&resampler_);
            av_packet_unref(audio_packet_);
            av_frame_unref(audio_frame_);
        }
#ifdef _WIN32
        audio_clock_valid_.store(false, std::memory_order_relaxed);
#endif
        return success();
    }

    std::vector<MediaSubtitleTrackInfo> subtitle_tracks() const override
    {
        std::vector<MediaSubtitleTrackInfo> tracks;
        if (!format_context_)
            return tracks;

        for (unsigned int index = 0; index < format_context_->nb_streams; ++index)
        {
            const AVStream* stream = format_context_->streams[index];
            if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
                continue;

            MediaSubtitleTrackInfo track;
            track.stream_index = static_cast<int>(index);
            track.codec = avcodec_get_name(stream->codecpar->codec_id);
            track.is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            track.is_selected = static_cast<int>(index) == subtitle_stream_index_;
            AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
            AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
            if (title)
                track.title = title->value;
            if (language)
                track.language = language->value;
            tracks.push_back(std::move(track));
        }
        return tracks;
    }

    MediaBackendResult select_subtitle_track(int stream_index) override
    {
        if (!opened_ || !format_context_)
            return failure("No media source is open.");
        if (playing_.load(std::memory_order_relaxed))
            return failure("Pause playback before switching the subtitle track.");
        if (stream_index == subtitle_stream_index_)
            return success();

        AVCodecContext* replacement_decoder = nullptr;
        if (stream_index >= 0 && !create_subtitle_decoder(stream_index, &replacement_decoder))
            return failure("Unable to open the requested subtitle decoder.");

        const int64_t resume_position = position_ms();
        const MediaBackendResult seek_result = seek(resume_position);
        if (!seek_result.success)
        {
            avcodec_free_context(&replacement_decoder);
            return seek_result;
        }

        {
            std::lock_guard<std::mutex> decode_lock(video_decode_mutex_);
            std::lock_guard<std::mutex> demux_lock(demux_mutex_);
            avcodec_free_context(&subtitle_decoder_);
            subtitle_decoder_ = replacement_decoder;
            subtitle_stream_index_ = stream_index;
            subtitle_filter_.reset();
            subtitle_filter_active_ = false;
            subtitle_filter_path_.clear();
            subtitle_filter_stream_ordinal_ = -1;
            subtitle_filter_width_ = 0;
            subtitle_filter_height_ = 0;
            if (stream_index >= 0)
                configure_ass_subtitle_filter(stream_index);
            active_subtitle_cues_.clear();
            for (AVPacket* packet : subtitle_packet_queue_)
                av_packet_free(&packet);
            subtitle_packet_queue_.clear();
            subtitle_packet_queue_bytes_ = 0;
        }
        return success();
    }

    MediaBackendResult set_external_subtitle(const std::string& path) override
    {
        if (!opened_ || !video_decoder_)
            return failure("No video source is open.");

        std::unique_ptr<SubtitleFilter> replacement;
        int width = 0;
        int height = 0;
        if (!path.empty())
        {
            replacement = std::make_unique<SubtitleFilter>();
            width = video_decoder_->width;
            height = video_decoder_->height;
            std::string error;
            if (!replacement->open(path, -1, width, height, error))
                return failure(error);
        }

        std::lock_guard<std::mutex> decode_lock(video_decode_mutex_);
        std::lock_guard<std::mutex> demux_lock(demux_mutex_);
        avcodec_free_context(&subtitle_decoder_);
        for (AVPacket* packet : subtitle_packet_queue_)
            av_packet_free(&packet);
        subtitle_packet_queue_.clear();
        subtitle_packet_queue_bytes_ = 0;
        active_subtitle_cues_.clear();
        subtitle_stream_index_ = -1;
        subtitle_filter_ = std::move(replacement);
        subtitle_filter_active_ = subtitle_filter_ != nullptr;
        subtitle_filter_path_ = path;
        subtitle_filter_stream_ordinal_ = -1;
        subtitle_filter_width_ = width;
        subtitle_filter_height_ = height;
        return success();
    }

    void set_subtitle_offset_ms(int64_t offset_ms) override
    {
        std::lock_guard<std::mutex> lock(demux_mutex_);
        subtitle_offset_ms_ = offset_ms;
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
        output.subtitle_text.clear();
        output.subtitle_bgra.clear();
        output.subtitle_width = 0;
        output.subtitle_height = 0;
        output.subtitle_stride = 0;
        output.rotation_degrees = 0;
        output.hdr_transfer = 0;
        output.color_primaries = 0;
        output.ycbcr_matrix = 0;
        output.ycbcr_full_range = 0;

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
            {
                const MediaBackendResult converted = convert_video_frame(output);
                if (converted.success)
                    update_subtitle_for_position(output.pts_ms, output);
                return converted;
            }

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
    struct DecodedSubtitleCue
    {
        struct BitmapRect
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            int stride = 0;
            std::vector<uint8_t> bgra;
        };

        int64_t start_ms = 0;
        int64_t end_ms = 0;
        std::string text;
        std::vector<BitmapRect> bitmaps;
    };

    static int normalize_rotation(double angle)
    {
        if (!std::isfinite(angle))
            return 0;
        const int nearest = static_cast<int>(std::lround(angle / 90.0)) * 90;
        if (std::abs(angle - nearest) > 2.0)
            return 0;
        int degrees = nearest % 360;
        if (degrees < 0)
            degrees += 360;
        return degrees;
    }

    static int get_stream_rotation_degrees(const AVStream* stream)
    {
        size_t side_data_size = 0;
        uint8_t* matrix = av_stream_get_side_data(
            stream, AV_PKT_DATA_DISPLAYMATRIX, &side_data_size);
        if (matrix && side_data_size >= sizeof(int32_t) * 9)
            return normalize_rotation(-av_display_rotation_get(
                reinterpret_cast<const int32_t*>(matrix)));

        AVDictionaryEntry* rotate = av_dict_get(stream->metadata, "rotate", nullptr, 0);
        if (rotate)
            return normalize_rotation(std::strtod(rotate->value, nullptr));
        return 0;
    }

    int get_frame_rotation_degrees(const AVFrame* frame) const
    {
        const AVFrameSideData* side_data = av_frame_get_side_data(
            frame, AV_FRAME_DATA_DISPLAYMATRIX);
        if (side_data && side_data->size >= sizeof(int32_t) * 9)
            return normalize_rotation(-av_display_rotation_get(
                reinterpret_cast<const int32_t*>(side_data->data)));
        return stream_rotation_degrees_;
    }

    static void rotate_bgra(VideoFrame& frame, int degrees)
    {
        if (degrees == 0 || frame.bgra.empty())
            return;

        const int source_width = frame.width;
        const int source_height = frame.height;
        const int source_stride = frame.stride;
        const int target_width = degrees == 180 ? source_width : source_height;
        const int target_height = degrees == 180 ? source_height : source_width;
        const int target_stride = target_width * 4;
        std::vector<uint8_t> rotated(static_cast<size_t>(target_stride) * target_height);

        for (int y = 0; y < source_height; ++y)
        {
            const uint8_t* source_row = frame.bgra.data() +
                static_cast<size_t>(y) * source_stride;
            for (int x = 0; x < source_width; ++x)
            {
                int target_x = x;
                int target_y = y;
                if (degrees == 90)
                {
                    target_x = source_height - 1 - y;
                    target_y = x;
                }
                else if (degrees == 180)
                {
                    target_x = source_width - 1 - x;
                    target_y = source_height - 1 - y;
                }
                else if (degrees == 270)
                {
                    target_x = y;
                    target_y = source_width - 1 - x;
                }

                const uint8_t* source_pixel = source_row + static_cast<size_t>(x) * 4;
                uint8_t* target_pixel = rotated.data() +
                    static_cast<size_t>(target_y) * target_stride +
                    static_cast<size_t>(target_x) * 4;
                std::copy_n(source_pixel, 4, target_pixel);
            }
        }

        frame.bgra = std::move(rotated);
        frame.width = target_width;
        frame.height = target_height;
        frame.stride = target_stride;
        if (degrees == 90 || degrees == 270)
            frame.display_aspect_ratio = frame.display_aspect_ratio > 0.0f
                ? 1.0f / frame.display_aspect_ratio
                : static_cast<float>(target_width) / target_height;
    }

    bool is_ass_subtitle_stream(int stream_index) const
    {
        if (!format_context_ || stream_index < 0 ||
            static_cast<unsigned int>(stream_index) >= format_context_->nb_streams)
            return false;
        const AVCodecID codec_id =
            format_context_->streams[stream_index]->codecpar->codec_id;
        return codec_id == AV_CODEC_ID_ASS || codec_id == AV_CODEC_ID_SSA;
    }

    int subtitle_stream_ordinal(int stream_index) const
    {
        int ordinal = 0;
        for (int index = 0; index < stream_index; ++index)
        {
            if (format_context_->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
                ++ordinal;
        }
        return ordinal;
    }

    void configure_ass_subtitle_filter(int stream_index)
    {
        if (!is_ass_subtitle_stream(stream_index))
            return;
        subtitle_filter_active_ = true;
        subtitle_filter_path_ = source_path_;
        subtitle_filter_stream_ordinal_ = subtitle_stream_ordinal(stream_index);
    }

    bool create_subtitle_decoder(int stream_index, AVCodecContext** output_decoder)
    {
        *output_decoder = nullptr;
        if (!format_context_ || stream_index < 0 ||
            static_cast<unsigned int>(stream_index) >= format_context_->nb_streams)
            return false;

        AVStream* stream = format_context_->streams[stream_index];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
            return false;

        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder)
            return false;

        AVCodecContext* context = avcodec_alloc_context3(decoder);
        if (!context)
            return false;

        int result = avcodec_parameters_to_context(context, stream->codecpar);
        if (result >= 0)
        {
            context->pkt_timebase = stream->time_base;
            result = avcodec_open2(context, decoder, nullptr);
        }
        if (result < 0)
        {
            avcodec_free_context(&context);
            return false;
        }

        *output_decoder = context;
        return true;
    }

    void update_subtitle_for_position(int64_t position_ms, VideoFrame& output)
    {
        std::unique_lock<std::mutex> demux_lock(demux_mutex_);
        const int64_t subtitle_position_ms = position_ms - subtitle_offset_ms_;
        if (subtitle_decoder_ && subtitle_stream_index_ >= 0)
        {
            AVStream* stream = format_context_->streams[subtitle_stream_index_];
            while (!subtitle_packet_queue_.empty())
            {
                AVPacket* packet = subtitle_packet_queue_.front();
                const int64_t packet_timestamp = packet->pts != AV_NOPTS_VALUE
                    ? av_rescale_q(packet->pts, stream->time_base, AVRational{1, 1000}) - start_time_ms_
                    : packet->dts != AV_NOPTS_VALUE
                        ? av_rescale_q(packet->dts, stream->time_base, AVRational{1, 1000}) - start_time_ms_
                        : position_ms;
                if (packet_timestamp > subtitle_position_ms)
                    break;

                subtitle_packet_queue_.pop_front();
                subtitle_packet_queue_bytes_ -= static_cast<size_t>(std::max(packet->size, 0));

                AVSubtitle subtitle{};
                int got_subtitle = 0;
                const int decoded = avcodec_decode_subtitle2(
                    subtitle_decoder_, &subtitle, &got_subtitle, packet);
                if (decoded >= 0 && got_subtitle)
                {
                    const int64_t base_ms = subtitle.pts != AV_NOPTS_VALUE
                        ? av_rescale_q(subtitle.pts, AV_TIME_BASE_Q, AVRational{1, 1000}) - start_time_ms_
                        : packet_timestamp;
                    DecodedSubtitleCue cue;
                    cue.start_ms = base_ms + subtitle.start_display_time;
                    cue.end_ms = base_ms + subtitle.end_display_time;
                    if (cue.end_ms <= cue.start_ms)
                    {
                        const int64_t packet_duration = packet->duration > 0
                            ? av_rescale_q(packet->duration, stream->time_base, AVRational{1, 1000})
                            : 0;
                        cue.end_ms = cue.start_ms + (packet_duration > 0 ? packet_duration : 5000);
                    }

                    for (unsigned int index = 0; index < subtitle.num_rects; ++index)
                    {
                        const AVSubtitleRect* rect = subtitle.rects[index];
                        if (rect->text && rect->text[0])
                        {
                            if (!cue.text.empty())
                                cue.text.push_back('\n');
                            cue.text += rect->text;
                        }
                        else if (rect->ass && rect->ass[0])
                        {
                            std::string ass = rect->ass;
                            const bool full_dialogue = ass.rfind("Dialogue:", 0) == 0;
                            if (full_dialogue)
                                ass.erase(0, 9);
                            size_t text_begin = 0;
                            const int metadata_commas = full_dialogue ? 9 : 8;
                            for (int field = 0; field < metadata_commas; ++field)
                            {
                                const size_t comma = ass.find(',', text_begin);
                                if (comma == std::string::npos)
                                {
                                    text_begin = ass.size();
                                    break;
                                }
                                text_begin = comma + 1;
                            }
                            if (text_begin < ass.size())
                            {
                                if (!cue.text.empty())
                                    cue.text.push_back('\n');
                                cue.text += ass.substr(text_begin);
                            }
                        }

                        if (rect->data[0] && rect->data[1] &&
                            rect->w > 0 && rect->h > 0 && rect->linesize[0] > 0)
                        {
                            DecodedSubtitleCue::BitmapRect bitmap;
                            bitmap.x = rect->x;
                            bitmap.y = rect->y;
                            bitmap.width = rect->w;
                            bitmap.height = rect->h;
                            bitmap.stride = rect->w * 4;
                            bitmap.bgra.resize(static_cast<size_t>(bitmap.stride) * bitmap.height);
                            const auto* palette =
                                reinterpret_cast<const uint32_t*>(rect->data[1]);
                            for (int row = 0; row < bitmap.height; ++row)
                            {
                                const uint8_t* indices = rect->data[0] +
                                    static_cast<size_t>(row) * rect->linesize[0];
                                uint8_t* pixels = bitmap.bgra.data() +
                                    static_cast<size_t>(row) * bitmap.stride;
                                for (int column = 0; column < bitmap.width; ++column)
                                {
                                    const uint32_t color = palette[indices[column]];
                                    pixels[column * 4 + 0] = static_cast<uint8_t>(color);
                                    pixels[column * 4 + 1] = static_cast<uint8_t>(color >> 8);
                                    pixels[column * 4 + 2] = static_cast<uint8_t>(color >> 16);
                                    pixels[column * 4 + 3] = static_cast<uint8_t>(color >> 24);
                                }
                            }
                            cue.bitmaps.push_back(std::move(bitmap));
                        }
                    }

                    const auto replace_all = [](std::string& value, const std::string& from,
                                                const std::string& to)
                    {
                        size_t position = 0;
                        while ((position = value.find(from, position)) != std::string::npos)
                        {
                            value.replace(position, from.size(), to);
                            position += to.size();
                        }
                    };
                    replace_all(cue.text, "\\N", "\n");
                    replace_all(cue.text, "\\n", "\n");
                    replace_all(cue.text, "\\h", " ");
                    const size_t tag_begin = cue.text.find('{');
                    if (tag_begin != std::string::npos)
                    {
                        size_t begin = tag_begin;
                        while (begin != std::string::npos)
                        {
                            const size_t end = cue.text.find('}', begin);
                            if (end == std::string::npos)
                                break;
                            cue.text.erase(begin, end - begin + 1);
                            begin = cue.text.find('{', begin);
                        }
                    }

                    if ((!cue.text.empty() || !cue.bitmaps.empty()) &&
                        cue.end_ms > cue.start_ms)
                    {
                        const auto insertion = std::upper_bound(
                            active_subtitle_cues_.begin(),
                            active_subtitle_cues_.end(),
                            cue.start_ms,
                            [](int64_t start, const DecodedSubtitleCue& existing)
                            {
                                return start < existing.start_ms;
                            });
                        active_subtitle_cues_.insert(insertion, std::move(cue));
                        while (active_subtitle_cues_.size() > 128)
                            active_subtitle_cues_.pop_front();
                    }
                    avsubtitle_free(&subtitle);
                }
                av_packet_free(&packet);
            }
        }

        while (!active_subtitle_cues_.empty() &&
            active_subtitle_cues_.front().end_ms <= subtitle_position_ms)
            active_subtitle_cues_.pop_front();

        bool has_bitmap = false;
        for (const DecodedSubtitleCue& cue : active_subtitle_cues_)
        {
            if (cue.start_ms <= subtitle_position_ms && subtitle_position_ms < cue.end_ms)
            {
                if (!cue.text.empty())
                {
                    if (!output.subtitle_text.empty())
                        output.subtitle_text.push_back('\n');
                    output.subtitle_text += cue.text;
                }
                has_bitmap = has_bitmap || !cue.bitmaps.empty();
            }
        }

        if (has_bitmap)
        {
            const int source_width =
                output.rotation_degrees == 90 || output.rotation_degrees == 270
                    ? output.height
                    : output.width;
            const int source_height =
                output.rotation_degrees == 90 || output.rotation_degrees == 270
                    ? output.width
                    : output.height;
            const int source_stride = source_width * 4;
            std::vector<uint8_t> overlay(
                static_cast<size_t>(source_stride) * source_height, 0);
            for (const DecodedSubtitleCue& cue : active_subtitle_cues_)
            {
                if (cue.start_ms > subtitle_position_ms ||
                    subtitle_position_ms >= cue.end_ms)
                    continue;
                for (const auto& bitmap : cue.bitmaps)
                {
                    for (int row = 0; row < bitmap.height; ++row)
                    {
                        const int target_y = bitmap.y + row;
                        if (target_y < 0 || target_y >= source_height)
                            continue;
                        const uint8_t* source = bitmap.bgra.data() +
                            static_cast<size_t>(row) * bitmap.stride;
                        for (int column = 0; column < bitmap.width; ++column)
                        {
                            const int target_x = bitmap.x + column;
                            if (target_x < 0 || target_x >= source_width)
                                continue;
                            const uint8_t* source_pixel = source + column * 4;
                            uint8_t* target_pixel = overlay.data() +
                                static_cast<size_t>(target_y) * source_stride +
                                static_cast<size_t>(target_x) * 4;
                            const unsigned int alpha = source_pixel[3];
                            const unsigned int inverse_alpha = 255 - alpha;
                            const unsigned int destination_alpha = target_pixel[3];
                            const unsigned int output_alpha =
                                alpha + (destination_alpha * inverse_alpha) / 255;
                            if (output_alpha > 0)
                            {
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    const unsigned int premultiplied =
                                        source_pixel[channel] * alpha +
                                        (target_pixel[channel] * destination_alpha * inverse_alpha) / 255;
                                    target_pixel[channel] = static_cast<uint8_t>(
                                        std::min(255u, (premultiplied + output_alpha / 2) / output_alpha));
                                }
                            }
                            target_pixel[3] = static_cast<uint8_t>(output_alpha);
                        }
                    }
                }
            }

            VideoFrame bitmap_frame;
            bitmap_frame.bgra = std::move(overlay);
            bitmap_frame.width = source_width;
            bitmap_frame.height = source_height;
            bitmap_frame.stride = source_stride;
            rotate_bgra(bitmap_frame, output.rotation_degrees);
            output.subtitle_bgra = std::move(bitmap_frame.bgra);
            output.subtitle_width = bitmap_frame.width;
            output.subtitle_height = bitmap_frame.height;
            output.subtitle_stride = bitmap_frame.stride;
        }

        const bool render_ass = subtitle_filter_active_;
        const int64_t subtitle_offset_ms = subtitle_offset_ms_;
        const std::string subtitle_path = subtitle_filter_path_;
        const int subtitle_stream_ordinal = subtitle_filter_stream_ordinal_;
        demux_lock.unlock();

        if (render_ass)
        {
            if (!subtitle_filter_ ||
                output.width != subtitle_filter_width_ ||
                output.height != subtitle_filter_height_)
            {
                subtitle_filter_ = std::make_unique<SubtitleFilter>();
                std::string error;
                if (!subtitle_filter_->open(
                    subtitle_path,
                    subtitle_stream_ordinal,
                    output.width,
                    output.height,
                    error))
                {
                    subtitle_filter_.reset();
                    subtitle_filter_active_ = false;
                    return;
                }
                subtitle_filter_width_ = output.width;
                subtitle_filter_height_ = output.height;
            }

            std::vector<uint8_t> rendered;
            int rendered_stride = 0;
            std::string error;
            const int64_t filter_position_ms = position_ms +
                (subtitle_stream_ordinal >= 0 ? start_time_ms_ : 0);
            if (subtitle_filter_->render(
                filter_position_ms, subtitle_offset_ms, rendered, rendered_stride, error))
            {
                output.subtitle_text.clear();
                output.subtitle_bgra = std::move(rendered);
                output.subtitle_width = output.width;
                output.subtitle_height = output.height;
                output.subtitle_stride = rendered_stride;
            }
            else
            {
                subtitle_filter_.reset();
                subtitle_filter_active_ = false;
            }
        }
    }

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

        if (!audio_decoder_)
        {
            std::lock_guard<std::mutex> clock_lock(video_clock_mutex_);
            position_ms_.store(video_clock_position_locked(), std::memory_order_relaxed);
            video_clock_running_ = false;
        }
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
            size_t* destination_bytes = nullptr;
            if (deferred_packet_->stream_index == audio_stream_index_ && audio_consumer_active_)
            {
                destination = &audio_packet_queue_;
                destination_bytes = &audio_packet_queue_bytes_;
            }
            else if (deferred_packet_->stream_index == video_stream_index_ && video_consumer_active_)
            {
                destination = &video_packet_queue_;
                destination_bytes = &video_packet_queue_bytes_;
            }
            else if (deferred_packet_->stream_index == subtitle_stream_index_ && subtitle_decoder_)
            {
                destination = &subtitle_packet_queue_;
                destination_bytes = &subtitle_packet_queue_bytes_;
            }

            if (destination)
            {
                constexpr size_t kMaximumQueuedPackets = 512;
                constexpr size_t kMaximumQueuedBytes = 64 * 1024 * 1024;
                size_t& queued_bytes = *destination_bytes;
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
            size_t* destination_bytes = nullptr;
            if (packet->stream_index == audio_stream_index_ && audio_consumer_active_)
            {
                destination = &audio_packet_queue_;
                destination_bytes = &audio_packet_queue_bytes_;
            }
            else if (packet->stream_index == video_stream_index_ && video_consumer_active_)
            {
                destination = &video_packet_queue_;
                destination_bytes = &video_packet_queue_bytes_;
            }
            else if (packet->stream_index == subtitle_stream_index_ && subtitle_decoder_)
            {
                destination = &subtitle_packet_queue_;
                destination_bytes = &subtitle_packet_queue_bytes_;
            }

            if (destination)
            {
                constexpr size_t kMaximumQueuedPackets = 512;
                constexpr size_t kMaximumQueuedBytes = 64 * 1024 * 1024;
                size_t& queued_bytes = *destination_bytes;
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
        for (AVPacket* packet : subtitle_packet_queue_)
            av_packet_free(&packet);
        av_packet_free(&deferred_packet_);
        audio_packet_queue_.clear();
        video_packet_queue_.clear();
        subtitle_packet_queue_.clear();
        audio_packet_queue_bytes_ = 0;
        video_packet_queue_bytes_ = 0;
        subtitle_packet_queue_bytes_ = 0;
        active_subtitle_cues_.clear();
    }

    MediaBackendResult convert_video_frame(VideoFrame& output)
    {
        AVFrame* source_frame = video_frame_;
        const int frame_rotation = get_frame_rotation_degrees(video_frame_);
        output.rotation_degrees = frame_rotation;
        const AVStream* video_stream = format_context_->streams[video_stream_index_];
        const AVColorTransferCharacteristic transfer =
            video_frame_->color_trc != AVCOL_TRC_UNSPECIFIED
                ? video_frame_->color_trc
                : video_stream->codecpar->color_trc;
        const AVColorPrimaries primaries =
            video_frame_->color_primaries != AVCOL_PRI_UNSPECIFIED
                ? video_frame_->color_primaries
                : video_stream->codecpar->color_primaries;
        output.hdr_transfer = transfer == AVCOL_TRC_SMPTE2084
            ? 1
            : transfer == AVCOL_TRC_ARIB_STD_B67 ? 2 : 0;
        output.color_primaries = primaries == AVCOL_PRI_BT2020 ? 1 : 0;
        const AVColorSpace frame_color_space =
            video_frame_->colorspace != AVCOL_SPC_UNSPECIFIED
                ? video_frame_->colorspace
                : video_stream->codecpar->color_space;
        const AVColorRange frame_color_range =
            video_frame_->color_range != AVCOL_RANGE_UNSPECIFIED
                ? video_frame_->color_range
                : video_stream->codecpar->color_range;
        output.ycbcr_matrix = frame_color_space == AVCOL_SPC_BT709
            ? 1
            : frame_color_space == AVCOL_SPC_BT2020_NCL ||
                frame_color_space == AVCOL_SPC_BT2020_CL
                ? 2
                : frame_color_space == AVCOL_SPC_UNSPECIFIED ||
                    frame_color_space == AVCOL_SPC_BT470BG ||
                    frame_color_space == AVCOL_SPC_SMPTE170M
                    ? 0
                    : 3;
        output.ycbcr_full_range = frame_color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        const bool hardware_matrix_supported =
            frame_color_space == AVCOL_SPC_UNSPECIFIED ||
            frame_color_space == AVCOL_SPC_BT709 ||
            frame_color_space == AVCOL_SPC_BT470BG ||
            frame_color_space == AVCOL_SPC_SMPTE170M;
#ifdef _WIN32
        if (video_frame_->format == AV_PIX_FMT_D3D11 &&
            video_device_shared_ && frame_rotation == 0 && output.hdr_transfer == 0 &&
            output.color_primaries == 0 && hardware_matrix_supported)
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

        int sws_color_space = SWS_CS_DEFAULT;
        if (frame_color_space == AVCOL_SPC_BT709)
            sws_color_space = SWS_CS_ITU709;
        else if (frame_color_space == AVCOL_SPC_BT2020_NCL ||
                 frame_color_space == AVCOL_SPC_BT2020_CL)
            sws_color_space = SWS_CS_BT2020;
        else if (frame_color_space == AVCOL_SPC_SMPTE240M)
            sws_color_space = SWS_CS_SMPTE240M;
        else if (frame_color_space == AVCOL_SPC_FCC)
            sws_color_space = SWS_CS_FCC;
        else if (frame_color_space == AVCOL_SPC_BT470BG ||
                 frame_color_space == AVCOL_SPC_SMPTE170M)
            sws_color_space = SWS_CS_ITU601;
        const int source_full_range = frame_color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        sws_setColorspaceDetails(
            video_scaler_,
            sws_getCoefficients(sws_color_space),
            source_full_range,
            sws_getCoefficients(SWS_CS_DEFAULT),
            1,
            0,
            1 << 16,
            1 << 16);

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
        rotate_bgra(output, frame_rotation);
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

    int64_t video_clock_position_locked() const
    {
        if (!video_clock_running_)
            return position_ms_.load(std::memory_order_relaxed);
        const auto elapsed = std::chrono::steady_clock::now() - video_clock_anchor_time_;
        const double elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        return video_clock_anchor_position_ms_ + static_cast<int64_t>(
            elapsed_ms * playback_rate_.load(std::memory_order_relaxed));
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
    mutable std::mutex video_clock_mutex_;
    std::chrono::steady_clock::time_point video_clock_anchor_time_ =
        std::chrono::steady_clock::now();
    int64_t video_clock_anchor_position_ms_ = 0;
    bool video_clock_running_ = false;
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
    AVCodecContext* subtitle_decoder_ = nullptr;
    std::unique_ptr<SubtitleFilter> subtitle_filter_;
    std::string source_path_;
    std::string subtitle_filter_path_;
    int subtitle_filter_stream_ordinal_ = -1;
    int subtitle_filter_width_ = 0;
    int subtitle_filter_height_ = 0;
    bool subtitle_filter_active_ = false;
    AVPacket* video_packet_ = nullptr;
    AVFrame* video_frame_ = nullptr;
    AVFrame* transfer_frame_ = nullptr;
    SwsContext* video_scaler_ = nullptr;
    AVBufferRef* hw_device_context_ = nullptr;
    bool video_hardware_decode_ = false;
    bool video_device_shared_ = false;
    std::deque<AVPacket*> audio_packet_queue_;
    std::deque<AVPacket*> video_packet_queue_;
    std::deque<AVPacket*> subtitle_packet_queue_;
    std::deque<DecodedSubtitleCue> active_subtitle_cues_;
    AVPacket* deferred_packet_ = nullptr;
    size_t audio_packet_queue_bytes_ = 0;
    size_t video_packet_queue_bytes_ = 0;
    size_t subtitle_packet_queue_bytes_ = 0;
    int audio_stream_index_ = -1;
    int video_stream_index_ = -1;
    int subtitle_stream_index_ = -1;
    int stream_rotation_degrees_ = 0;
    int64_t subtitle_offset_ms_ = 0;
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
    int ycbcr_matrix,
    int ycbcr_full_range,
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

    int sws_color_space = ycbcr_matrix == 1 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    if (ycbcr_matrix > 1)
    {
        if (hardware_frame->colorspace == AVCOL_SPC_BT2020_NCL ||
            hardware_frame->colorspace == AVCOL_SPC_BT2020_CL)
            sws_color_space = SWS_CS_BT2020;
        else if (hardware_frame->colorspace == AVCOL_SPC_SMPTE240M)
            sws_color_space = SWS_CS_SMPTE240M;
        else if (hardware_frame->colorspace == AVCOL_SPC_FCC)
            sws_color_space = SWS_CS_FCC;
    }
    sws_setColorspaceDetails(
        scaler,
        sws_getCoefficients(sws_color_space),
        ycbcr_full_range,
        sws_getCoefficients(SWS_CS_DEFAULT),
        1,
        0,
        1 << 16,
        1 << 16);

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
