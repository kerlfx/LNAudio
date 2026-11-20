#include "ffmpeg_media_backend.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace lnplayer
{
namespace
{

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

        const int open_result = avformat_open_input(&format_context_, source.c_str(), nullptr, nullptr);
        if (open_result < 0)
            return failure("Unable to open media source: " + ffmpeg_error(open_result));

        const int stream_result = avformat_find_stream_info(format_context_, nullptr);
        if (stream_result < 0)
        {
            const std::string message = "Unable to read media stream information: " +
                ffmpeg_error(stream_result);
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

        opened_ = true;
        playing_ = false;
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
        if (format_context_)
            avformat_close_input(&format_context_);

        audio_stream_index_ = -1;
        video_stream_index_ = -1;
        opened_ = false;
        playing_ = false;
        return success();
    }

private:
    static MediaBackendResult success()
    {
        return MediaBackendResult{true, {}};
    }

    static MediaBackendResult failure(std::string message)
    {
        return MediaBackendResult{false, std::move(message)};
    }

    AVFormatContext* format_context_ = nullptr;
    int audio_stream_index_ = -1;
    int video_stream_index_ = -1;
    bool opened_ = false;
    bool playing_ = false;
};

}

std::unique_ptr<MediaBackend> create_ffmpeg_media_backend()
{
    return std::make_unique<FFmpegMediaBackend>();
}

}
