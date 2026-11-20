#ifndef LNPLAYER_MEDIA_BACKEND_H
#define LNPLAYER_MEDIA_BACKEND_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lnplayer
{

struct MediaInfo
{
    int64_t duration_ms = -1;
    bool has_audio = false;
    bool has_video = false;
};

struct MediaTrackInfo
{
    int stream_index = -1;
    std::string title;
    std::string language;
    std::string codec;
    int channels = 0;
    int sample_rate = 0;
    bool is_default = false;
    bool is_selected = false;
};

struct MediaSubtitleTrackInfo
{
    int stream_index = -1;
    std::string title;
    std::string language;
    std::string codec;
    bool is_default = false;
    bool is_selected = false;
};

struct MediaBackendResult
{
    bool success = false;
    std::string message;
};

struct AudioFrame
{
    std::vector<float> samples;
    int sample_rate = 48000;
    int channels = 2;
    int64_t pts_ms = 0;
};

struct VideoFrame
{
    std::vector<uint8_t> bgra;
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t pts_ms = 0;
    uint64_t generation = 0;
    float display_aspect_ratio = 0.0f;
    int rotation_degrees = 0;
    int hdr_transfer = 0;
    int color_primaries = 0;
    int ycbcr_matrix = 0;
    int ycbcr_full_range = 0;
    std::string subtitle_text;
    std::vector<uint8_t> subtitle_bgra;
    int subtitle_width = 0;
    int subtitle_height = 0;
    int subtitle_stride = 0;
#ifdef _WIN32
    void* d3d11_texture = nullptr;
    uint32_t d3d11_array_slice = 0;
    uint32_t dxgi_format = 0;
    std::shared_ptr<void> hardware_frame_owner;
#endif
};

class MediaBackend
{
public:
#ifdef _WIN32
    virtual void set_video_device(void* device) = 0;
#endif
    virtual ~MediaBackend() = default;

    virtual MediaBackendResult open(const std::string& source, MediaInfo& info) = 0;
    virtual MediaBackendResult play() = 0;
    virtual MediaBackendResult pause() = 0;
    virtual MediaBackendResult seek(int64_t position_ms) = 0;
    virtual MediaBackendResult stop() = 0;
    virtual MediaBackendResult close() = 0;
    virtual MediaBackendResult read_audio_frame(AudioFrame& frame, bool& end_of_stream) = 0;
    virtual MediaBackendResult read_video_frame(VideoFrame& frame, bool& end_of_stream) = 0;
    virtual std::vector<MediaTrackInfo> audio_tracks() const { return {}; }
    virtual MediaBackendResult select_audio_track(int)
    {
        return MediaBackendResult{false, "Audio track selection is not supported by this backend."};
    }
    virtual std::vector<MediaSubtitleTrackInfo> subtitle_tracks() const { return {}; }
    virtual MediaBackendResult select_subtitle_track(int)
    {
        return MediaBackendResult{false, "Subtitle track selection is not supported by this backend."};
    }
    virtual MediaBackendResult set_external_subtitle(const std::string&)
    {
        return MediaBackendResult{false, "External subtitle rendering is not supported by this backend."};
    }
    virtual void set_subtitle_offset_ms(int64_t) {}
    virtual int64_t position_ms() const = 0;
    virtual bool playback_ended() const = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_playback_rate(float rate) = 0;
    virtual void set_playback_end_callback(std::function<void()> callback) = 0;
    virtual bool playback_failed() const = 0;
    virtual void set_playback_error_callback(std::function<void(std::string)> callback) = 0;
};

using MediaBackendFactory = std::unique_ptr<MediaBackend> (*)();

}

#endif
