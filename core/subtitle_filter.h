#ifndef LNPLAYER_SUBTITLE_FILTER_H
#define LNPLAYER_SUBTITLE_FILTER_H

#include <cstdint>
#include <string>
#include <vector>

struct AVFilterContext;
struct AVFilterGraph;

namespace lnplayer
{

class SubtitleFilter
{
public:
    SubtitleFilter() = default;
    ~SubtitleFilter();

    SubtitleFilter(const SubtitleFilter&) = delete;
    SubtitleFilter& operator=(const SubtitleFilter&) = delete;

    bool open(
        const std::string& subtitle_path,
        int subtitle_stream_ordinal,
        int width,
        int height,
        std::string& error);
    bool render(
        int64_t pts_ms,
        int64_t offset_ms,
        std::vector<uint8_t>& bgra,
        int& stride,
        std::string& error);
    void close();

private:
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* source_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    std::string subtitle_path_;
    int subtitle_stream_ordinal_ = -1;
    int width_ = 0;
    int height_ = 0;
};

}

#endif
