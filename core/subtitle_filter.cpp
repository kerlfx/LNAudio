#include "subtitle_filter.h"

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

namespace lnplayer
{
namespace
{

std::string detect_subtitle_encoding(const std::string& path)
{
    AVIOContext* io = nullptr;
    if (avio_open2(&io, path.c_str(), AVIO_FLAG_READ, nullptr, nullptr) < 0)
        return {};

    uint8_t bom[4]{};
    const int bytes_read = avio_read(io, bom, sizeof(bom));
    avio_closep(&io);

    if (bytes_read >= 4 &&
        bom[0] == 0xFF && bom[1] == 0xFE && bom[2] == 0x00 && bom[3] == 0x00)
        return "UTF-32LE";
    if (bytes_read >= 4 &&
        bom[0] == 0x00 && bom[1] == 0x00 && bom[2] == 0xFE && bom[3] == 0xFF)
        return "UTF-32BE";
    if (bytes_read >= 3 &&
        bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
        return "UTF-8";
    if (bytes_read >= 2 && bom[0] == 0xFF && bom[1] == 0xFE)
        return "UTF-16LE";
    if (bytes_read >= 2 && bom[0] == 0xFE && bom[1] == 0xFF)
        return "UTF-16BE";
    return {};
}

}

SubtitleFilter::~SubtitleFilter()
{
    close();
}

bool SubtitleFilter::open(
    const std::string& subtitle_path,
    int subtitle_stream_ordinal,
    int width,
    int height,
    std::string& error)
{
    close();
    if (subtitle_path.empty() || width <= 0 || height <= 0)
    {
        error = "Invalid subtitle filter input.";
        return false;
    }

    graph_ = avfilter_graph_alloc();
    if (!graph_)
    {
        error = "Unable to allocate FFmpeg subtitle filter graph.";
        return false;
    }
    graph_->nb_threads = 1;

    const AVFilter* buffer_filter = avfilter_get_by_name("buffer");
    const AVFilter* subtitles_filter = avfilter_get_by_name("subtitles");
    const AVFilter* sink_filter = avfilter_get_by_name("buffersink");
    if (!buffer_filter || !subtitles_filter || !sink_filter)
    {
        error = "FFmpeg was built without the required libass subtitle filters.";
        close();
        return false;
    }

    const std::string source_args =
        "video_size=" + std::to_string(width) + "x" + std::to_string(height) +
        ":pix_fmt=" + std::to_string(AV_PIX_FMT_BGRA) +
        ":time_base=1/1000:pixel_aspect=1/1";

    int result = avfilter_graph_create_filter(
        &source_,
        buffer_filter,
        "subtitle_input",
        source_args.c_str(),
        nullptr,
        graph_);
    if (result < 0)
    {
        error = "Unable to create subtitle filter input.";
        close();
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "filename", subtitle_path.c_str(), 0);
    av_dict_set_int(&options, "alpha", 1, 0);
    if (subtitle_stream_ordinal >= 0)
        av_dict_set_int(&options, "si", subtitle_stream_ordinal, 0);
    else
    {
        const std::string encoding = detect_subtitle_encoding(subtitle_path);
        if (!encoding.empty())
            av_dict_set(&options, "charenc", encoding.c_str(), 0);
    }

    AVFilterContext* subtitles = nullptr;
    result = avfilter_graph_create_filter(
        &subtitles,
        subtitles_filter,
        "libass_subtitles",
        nullptr,
        &options,
        graph_);
    av_dict_free(&options);
    if (result < 0)
    {
        error = "Unable to initialize FFmpeg libass subtitle renderer.";
        close();
        return false;
    }

    result = avfilter_graph_create_filter(
        &sink_,
        sink_filter,
        "subtitle_output",
        nullptr,
        nullptr,
        graph_);
    if (result < 0)
    {
        error = "Unable to create subtitle filter output.";
        close();
        return false;
    }

    result = avfilter_link(source_, 0, subtitles, 0);
    if (result >= 0)
        result = avfilter_link(subtitles, 0, sink_, 0);
    if (result >= 0)
        result = avfilter_graph_config(graph_, nullptr);
    if (result < 0)
    {
        error = "Unable to configure FFmpeg libass subtitle renderer.";
        close();
        return false;
    }

    subtitle_path_ = subtitle_path;
    subtitle_stream_ordinal_ = subtitle_stream_ordinal;
    width_ = width;
    height_ = height;
    error.clear();
    return true;
}

bool SubtitleFilter::render(
    int64_t pts_ms,
    int64_t offset_ms,
    std::vector<uint8_t>& bgra,
    int& stride,
    std::string& error)
{
    bgra.clear();
    stride = 0;
    if (!graph_ || !source_ || !sink_)
    {
        error = "Subtitle filter is not initialized.";
        return false;
    }

    AVFrame* input = av_frame_alloc();
    if (!input)
    {
        error = "Unable to allocate subtitle filter input frame.";
        return false;
    }

    input->format = AV_PIX_FMT_BGRA;
    input->width = width_;
    input->height = height_;
    input->pts = pts_ms - offset_ms;
    int result = av_frame_get_buffer(input, 32);
    if (result < 0)
    {
        av_frame_free(&input);
        error = "Unable to allocate subtitle filter frame buffer.";
        return false;
    }

    for (int row = 0; row < height_; ++row)
        std::memset(input->data[0] + row * input->linesize[0], 0,
            static_cast<size_t>(width_) * 4);

    result = av_buffersrc_add_frame_flags(source_, input, AV_BUFFERSRC_FLAG_KEEP_REF);
    av_frame_free(&input);
    if (result < 0)
    {
        error = "Unable to submit frame to subtitle renderer.";
        return false;
    }

    AVFrame* output = av_frame_alloc();
    if (!output)
    {
        error = "Unable to allocate subtitle filter output frame.";
        return false;
    }

    result = av_buffersink_get_frame(sink_, output);
    if (result < 0)
    {
        av_frame_free(&output);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        {
            error.clear();
            return true;
        }
        error = "Unable to receive rendered subtitle frame.";
        return false;
    }

    if (output->format != AV_PIX_FMT_BGRA ||
        output->width != width_ || output->height != height_)
    {
        av_frame_free(&output);
        error = "Subtitle renderer returned an unexpected pixel format or size.";
        return false;
    }

    stride = width_ * 4;
    bgra.resize(static_cast<size_t>(stride) * height_);
    for (int row = 0; row < height_; ++row)
    {
        std::copy_n(
            output->data[0] + row * output->linesize[0],
            stride,
            bgra.data() + static_cast<size_t>(row) * stride);
    }

    av_frame_free(&output);
    error.clear();
    return true;
}

void SubtitleFilter::close()
{
    avfilter_graph_free(&graph_);
    source_ = nullptr;
    sink_ = nullptr;
    subtitle_path_.clear();
    subtitle_stream_ordinal_ = -1;
    width_ = 0;
    height_ = 0;
}

}
