#ifndef LNPLAYER_VIDEO_SURFACE_H
#define LNPLAYER_VIDEO_SURFACE_H

#include <QImage>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>

namespace lnplayer::windows
{
class D3D11VideoRenderer;
}

class VideoSurface final : public QWidget
{
public:
    explicit VideoSurface(QWidget* parent = nullptr);
    ~VideoSurface() override;

    void set_frame(const QImage& frame);
    bool has_renderer() const;
    void* native_device() const;
    void set_stretch(bool enabled);
    void set_aspect_ratio(float ratio);
    void set_source_aspect_ratio(float ratio);
    void set_video_filter(
        float brightness, float contrast, float saturation, float hue_degrees,
        float sharpness, float denoise, bool grayscale);
    void set_hdr_metadata(int transfer, int color_primaries);
    void set_video_color_space(int ycbcr_matrix, int full_range);
    bool present_frame_threadsafe(const QImage& frame);
    bool present_hardware_frame(
        void* texture, uint32_t array_slice, uint32_t format,
        int width, int height, std::shared_ptr<void> frame_owner,
        const QImage& overlay);
    QImage capture_frame() const;
    void set_interaction_callbacks(
        std::function<void()> double_click,
        std::function<void(int)> wheel);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QImage frame_;
    lnplayer::windows::D3D11VideoRenderer* renderer_ = nullptr;
    bool stretch_ = false;
    float aspect_ratio_ = 0.0f;
    float source_aspect_ratio_ = 0.0f;
    std::function<void()> double_click_callback_;
    std::function<void(int)> wheel_callback_;
};

#endif
