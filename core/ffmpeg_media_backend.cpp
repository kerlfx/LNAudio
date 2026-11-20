#include "ffmpeg_media_backend.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <limits>
#include <string>
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
        audio_draining_ = false;
        audio_packet_pending_ = false;
        return success();
    }

    MediaBackendResult play() override
    {
        if (!opened_)
            return failure("No media source is open.");
        playing_ = true;
        return success();
    }

    MediaBackendResult pause() override
    {
        if (!opened_ || !playing_)
            return failure("The media source is not playing.");
        playing_ = false;
        return success();
    }

    MediaBackendResult seek(int64_t position_ms) override
    {
        if (!opened_ || position_ms < 0)
            return failure("The requested seek position is invalid.");

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
            return failure("Unable to seek media source: " + ffmpeg_error(result));

        avformat_flush(format_context_);
        if (audio_decoder_)
            avcodec_flush_buffers(audio_decoder_);
        if (audio_packet_)
            av_packet_unref(audio_packet_);
        audio_packet_pending_ = false;
        audio_draining_ = false;
        audio_decoder_eof_ = false;
        if (resampler_)
        {
            swr_close(resampler_);
            const int resampler_result = swr_init(resampler_);
            if (resampler_result < 0)
                return failure("Unable to reset audio resampler: " + ffmpeg_error(resampler_result));
        }
        return success();
    }

    MediaBackendResult stop() override
    {
        if (!opened_)
            return success();

        const MediaBackendResult result = seek(0);
        if (result.success)
            playing_ = false;
        return result;
    }

    MediaBackendResult close() override
    {
        swr_free(&resampler_);
        av_frame_free(&audio_frame_);
        av_packet_free(&audio_packet_);
        avcodec_free_context(&audio_decoder_);
        if (format_context_)
            avformat_close_input(&format_context_);

        audio_stream_index_ = -1;
        video_stream_index_ = -1;
        opened_ = false;
        playing_ = false;
        audio_draining_ = false;
        audio_packet_pending_ = false;
        audio_decoder_eof_ = false;
        return success();
    }

    MediaBackendResult read_audio_frame(AudioFrame& output, bool& end_of_stream) override
    {
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
                kOutputSampleRate,
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
            kOutputSampleRate,
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
        output.sample_rate = kOutputSampleRate;
        output.channels = kOutputChannels;

        const AVStream* stream = format_context_->streams[audio_stream_index_];
        const int64_t timestamp = audio_frame_->best_effort_timestamp;
        output.pts_ms = timestamp == AV_NOPTS_VALUE
            ? 0
            : av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000});

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
    AVCodecContext* audio_decoder_ = nullptr;
    AVPacket* audio_packet_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    int audio_stream_index_ = -1;
    int video_stream_index_ = -1;
    bool opened_ = false;
    bool playing_ = false;
    bool audio_draining_ = false;
    bool audio_packet_pending_ = false;
};

}

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend()
{
    return std::make_unique<FFmpegMediaBackend>();
}

}
