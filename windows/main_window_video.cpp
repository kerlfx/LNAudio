#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"

#include "lnplayer_windows.h"
#include "player_icons.h"

#include <algorithm>
#include <d3d11.h>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QFont>
#include <QFontMetrics>
#include <QFontComboBox>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QCheckBox>
#include <QColor>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScreen>
#include <QDropEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QListView>
#include <QListWidgetItem>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyleOptionSlider>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QShortcut>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QStringDecoder>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif


namespace
{
 format_position(int64_t position_ms)
{
    const int64_t total_seconds = position_ms < 0 ? 0 : position_ms / 1000;
    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = total_seconds / 60 % 60;
    const int64_t seconds = total_seconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

 application_data_directory()
{
    static const QString directory = []
    {
        const QString application_directory = QCoreApplication::applicationDirPath();
        const QString portable_directory = QDir(application_directory).filePath(
            QStringLiteral("Data"));
        if (QDir().mkpath(portable_directory))
        {
            QFile write_probe(QDir(portable_directory).filePath(
                QStringLiteral(".write_test")));
            if (write_probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                write_probe.close();
                QFile::remove(write_probe.fileName());
                return portable_directory;
            }
        }

        const QString local_directory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        const QString fallback_directory = QDir(local_directory).filePath(
            QStringLiteral("Data"));
        QDir().mkpath(fallback_directory);
        return fallback_directory;
    }();
    return directory;
}

 settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}

 draw_subtitle_text(
    QImage& image,
    const QString& subtitle,
    const QString& font_family,
    const QString& color,
    int font_size,
    int vertical_position)
{
    if (image.isNull() || subtitle.isEmpty())
        return;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(font_family);
    font.setPixelSize(font_size > 0 ? font_size : std::max(20, image.width() / 40));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);

    const QRect text_rect(
        image.width() / 20,
        image.height() * vertical_position / 100,
        image.width() * 9 / 10,
        image.height() / 6);
    const int offsets[][2] = {
        {-2, -2}, {0, -2}, {2, -2}, {-2, 0},
        {2, 0}, {-2, 2}, {0, 2}, {2, 2}
    };
    painter.setPen(Qt::black);
    for (const auto& offset : offsets)
        painter.drawText(
            text_rect.translated(offset[0], offset[1]),
            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            subtitle);
    painter.setPen(QColor(color));
    painter.drawText(
        text_rect,
        Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
        subtitle);
}

 draw_subtitle_bitmap(QImage& image, const QImage& bitmap, bool fit_aspect, float aspect)
{
    if (image.isNull() || bitmap.isNull())
        return;

    QPainter painter(&image);
    if (!fit_aspect)
    {
        painter.drawImage(image.rect(), bitmap);
        return;
    }

    if (aspect <= 0.0f)
        aspect = static_cast<float>(bitmap.width()) / bitmap.height();
    int target_width = image.width();
    int target_height = static_cast<int>(target_width / aspect);
    if (target_height > image.height())
    {
        target_height = image.height();
        target_width = static_cast<int>(target_height * aspect);
    }
    const QRect target(
        (image.width() - target_width) / 2,
        (image.height() - target_height) / 2,
        target_width,
        target_height);
    painter.drawImage(target, bitmap);
}
}

void MainWindow::requestVideoFrame()
{
    if (mode_ != PlayerMode::Video || !has_video_ || video_frame_pending_ ||
        video_seek_pending_ || video_end_of_stream_)
        return;

    video_frame_pending_ = true;
    postFrame([this]
    {
        if (!player_)
        {
            QMetaObject::invokeMethod(this, [this]
            {
                video_frame_pending_ = false;
            }, Qt::QueuedConnection);
            return;
        }

        lnplayer_snapshot_t request_snapshot{};
        request_snapshot.struct_size = sizeof(request_snapshot);
        if (lnplayer_get_snapshot(player_, &request_snapshot) != LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this]
            {
                video_frame_pending_ = false;
            }, Qt::QueuedConnection);
            return;
        }
        const uint64_t requested_generation = request_snapshot.generation;

        struct FrameContext
        {
            QImage image;
            std::shared_ptr<void> hardware_owner;
            void* hardware_texture = nullptr;
            uint32_t hardware_array_slice = 0;
            uint32_t hardware_format = 0;
            int hardware_width = 0;
            int hardware_height = 0;
            int64_t pts_ms = 0;
            uint64_t generation = 0;
            float display_aspect_ratio = 0.0f;
            int hdr_transfer = 0;
            int color_primaries = 0;
            float hdr_peak_luminance = 1000.0f;
            int ycbcr_matrix = 0;
            int ycbcr_full_range = 0;
            QString subtitle;
            QImage subtitle_bitmap;
        } context;

        int end_of_stream = 0;
        const lnplayer_result_t result = lnplayer_windows_read_video_frame(
            player_,
            +[](const lnplayer_windows_video_frame_t* frame, void* user_data)
            {
                auto* context = static_cast<FrameContext*>(user_data);
                context->pts_ms = frame->pts_ms;
                context->generation = frame->generation;
                context->display_aspect_ratio = frame->display_aspect_ratio;
                context->hdr_transfer = frame->hdr_transfer;
                context->color_primaries = frame->color_primaries;
                context->hdr_peak_luminance = frame->hdr_peak_luminance;
                context->ycbcr_matrix = frame->ycbcr_matrix;
                context->ycbcr_full_range = frame->ycbcr_full_range;
                context->subtitle = QString::fromUtf8(
                    frame->subtitle_utf8 ? frame->subtitle_utf8 : "");
                if (frame->subtitle_bgra && frame->subtitle_width > 0 &&
                    frame->subtitle_height > 0 &&
                    frame->subtitle_stride >= frame->subtitle_width * 4)
                {
                    context->subtitle_bitmap = QImage(
                        frame->subtitle_bgra,
                        frame->subtitle_width,
                        frame->subtitle_height,
                        frame->subtitle_stride,
                        QImage::Format_ARGB32).copy();
                }
                if (frame->d3d11_texture)
                {
                    void* retained_owner = nullptr;
                    if (lnplayer_windows_retain_video_frame(frame, &retained_owner) ==
                        LNPLAYER_RESULT_OK)
                    {
                        context->hardware_owner = std::shared_ptr<void>(
                            retained_owner, lnplayer_windows_release_video_frame);
                        context->hardware_texture = frame->d3d11_texture;
                        context->hardware_array_slice = frame->d3d11_array_slice;
                        context->hardware_format = frame->dxgi_format;
                        context->hardware_width = frame->width;
                        context->hardware_height = frame->height;
                    }
                }
                else
                {
                    context->image = QImage(
                        frame->bgra,
                        frame->width,
                        frame->height,
                        frame->stride,
                        QImage::Format_ARGB32).copy();
                }
            },
            &context,
            &end_of_stream);

        const QImage image = context.image;
        const auto hardware_owner = context.hardware_owner;
        const void* hardware_texture = context.hardware_texture;
        const uint32_t hardware_array_slice = context.hardware_array_slice;
        const uint32_t hardware_format = context.hardware_format;
        const int hardware_width = context.hardware_width;
        const int hardware_height = context.hardware_height;
        const int64_t pts_ms = context.pts_ms;
        const uint64_t generation = context.generation;
        const float display_aspect_ratio = context.display_aspect_ratio;
        const int hdr_transfer = context.hdr_transfer;
        const int color_primaries = context.color_primaries;
        const float hdr_peak_luminance = context.hdr_peak_luminance;
        const int ycbcr_matrix = context.ycbcr_matrix;
        const int ycbcr_full_range = context.ycbcr_full_range;
        const QString embedded_subtitle = context.subtitle;
        const QImage embedded_subtitle_bitmap = context.subtitle_bitmap;
        QMetaObject::invokeMethod(this,
            [this, result, end_of_stream, image, hardware_owner, hardware_texture,
             hardware_array_slice, hardware_format, hardware_width, hardware_height,
             pts_ms, generation, requested_generation, display_aspect_ratio,
             hdr_transfer, color_primaries, hdr_peak_luminance,
             ycbcr_matrix, ycbcr_full_range,
             embedded_subtitle, embedded_subtitle_bitmap]
        {
            video_frame_pending_ = false;
            if (result != LNPLAYER_RESULT_OK)
            {
                if (is_playing_)
                    status_label_->setText(QStringLiteral("视频解码失败"));
                return;
            }

            if (end_of_stream)
            {
                if (!video_seek_pending_ && requested_generation >= media_generation_)
                {
                    media_generation_ = requested_generation;
                    video_end_of_stream_ = true;
                    video_timer_->stop();
                }
                return;
            }

            if ((!image.isNull() || hardware_owner) && !video_seek_pending_ &&
                generation == requested_generation && generation >= media_generation_)
            {
                media_generation_ = generation;
                video_seek_pending_ = false;
                video_end_of_stream_ = false;
                video_first_frame_received_ = true;
                if (empty_state_label_)
                    empty_state_label_->hide();
                if (video_surface_)
                {
                    video_surface_->set_hdr_metadata(
                        hdr_transfer, color_primaries, hdr_peak_luminance);
                    video_surface_->set_video_color_space(ycbcr_matrix, ycbcr_full_range);
                    video_surface_->set_source_aspect_ratio(display_aspect_ratio);
                }
                pending_video_frame_ = image;
                const bool first_video_frame = video_width_ <= 0 || video_height_ <= 0;
                video_width_ = hardware_owner ? hardware_width : image.width();
                video_height_ = hardware_owner ? hardware_height : image.height();
                if (first_video_frame && display_aspect_ratio > 0.0f)
                {
                    fitWindowToVideoAspect(display_aspect_ratio, QSize(video_width_, video_height_));
                }
                pending_hardware_frame_owner_ = hardware_owner;
                pending_hardware_texture_ = const_cast<void*>(hardware_texture);
                pending_hardware_array_slice_ = hardware_array_slice;
                pending_hardware_format_ = hardware_format;
                pending_hardware_width_ = hardware_width;
                pending_hardware_height_ = hardware_height;
                pending_video_pts_ms_ = pts_ms;
                pending_video_generation_ = generation;
                pending_embedded_subtitle_ = embedded_subtitle;
                pending_embedded_subtitle_bitmap_ = embedded_subtitle_bitmap;
                pending_video_display_aspect_ratio_ = display_aspect_ratio;
                pending_hdr_transfer_ = hdr_transfer;
                pending_color_primaries_ = color_primaries;
                pending_hdr_peak_luminance_ = hdr_peak_luminance;
                pending_video_ycbcr_matrix_ = ycbcr_matrix;
                pending_video_ycbcr_full_range_ = ycbcr_full_range;
                pending_video_frame_valid_ = true;
                presentPendingVideoFrame();
            }
            else if (!video_seek_pending_ && generation != requested_generation)
            {
                requestVideoFrame();
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::scheduleVideoPresentation()
{
    if (!pending_video_frame_valid_ || !is_playing_ || !video_timer_)
        return;

    double clock_ms = static_cast<double>(current_position_ms_);
    if (video_clock_timer_.isValid())
        clock_ms = static_cast<double>(video_clock_anchor_position_ms_) +
            video_clock_timer_.nsecsElapsed() / 1000000.0 * playback_rate_;

    const double remaining_ms =
        (static_cast<double>(pending_video_pts_ms_) - clock_ms - 8.0) /
        std::max(0.25, static_cast<double>(playback_rate_));
    const int delay_ms = static_cast<int>(std::clamp(
        std::ceil(remaining_ms), 0.0, 1000.0));
    video_timer_->start(delay_ms);
}

void MainWindow::presentPendingVideoFrame()
{
    if (!pending_video_frame_valid_ || !video_surface_)
        return;

    if (is_playing_)
    {
        double clock_ms = static_cast<double>(current_position_ms_);
        if (video_clock_timer_.isValid())
            clock_ms = static_cast<double>(video_clock_anchor_position_ms_) +
                video_clock_timer_.nsecsElapsed() / 1000000.0 * playback_rate_;
        if (static_cast<double>(pending_video_pts_ms_) > clock_ms + 8.0)
        {
            scheduleVideoPresentation();
            return;
        }
    }

    viewing_historical_frame_ = false;
    const QString media_path = current_media_path_;
    const QString font_family = subtitle_font_family_;
    const QString subtitle_color = subtitle_color_;
    const int font_size = subtitle_font_size_;
    const int vertical_position = subtitle_vertical_position_;

    if (pending_hardware_frame_owner_)
    {
        QString subtitle = subtitleAt(pending_video_pts_ms_);
        if (subtitles_enabled_ && !pending_embedded_subtitle_.isEmpty())
        {
            if (!subtitle.isEmpty())
                subtitle += QLatin1Char('\n');
            subtitle += pending_embedded_subtitle_;
        }

        VideoFrameHistoryEntry history_entry;
        history_entry.hardware_owner = pending_hardware_frame_owner_;
        history_entry.hardware_texture = pending_hardware_texture_;
        history_entry.hardware_array_slice = pending_hardware_array_slice_;
        history_entry.hardware_format = pending_hardware_format_;
        history_entry.hardware_width = pending_hardware_width_;
        history_entry.hardware_height = pending_hardware_height_;
        history_entry.hdr_transfer = pending_hdr_transfer_;
        history_entry.color_primaries = pending_color_primaries_;
        history_entry.hdr_peak_luminance = pending_hdr_peak_luminance_;
        history_entry.display_aspect_ratio = pending_video_display_aspect_ratio_;
        history_entry.ycbcr_matrix = pending_video_ycbcr_matrix_;
        history_entry.ycbcr_full_range = pending_video_ycbcr_full_range_;
        history_entry.embedded_subtitle = pending_embedded_subtitle_;
        history_entry.embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
        history_entry.pts_ms = pending_video_pts_ms_;
        history_entry.generation = pending_video_generation_;
        if (video_frame_history_index_ + 1 < video_frame_history_.size())
            video_frame_history_.erase(
                video_frame_history_.begin() + video_frame_history_index_ + 1,
                video_frame_history_.end());
        video_frame_history_.append(std::move(history_entry));
        trimVideoFrameHistory();
        video_frame_history_index_ = video_frame_history_.size() - 1;

        auto owner = std::move(pending_hardware_frame_owner_);
        void* texture = pending_hardware_texture_;
        const uint32_t array_slice = pending_hardware_array_slice_;
        const uint32_t format = pending_hardware_format_;
        const int width = pending_hardware_width_;
        const int height = pending_hardware_height_;
        const uint64_t generation = pending_video_generation_;
        const int64_t pts_ms = pending_video_pts_ms_;
        const QImage embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
        const bool render_subtitles = subtitles_enabled_;
        const int ycbcr_matrix = pending_video_ycbcr_matrix_;
        const int ycbcr_full_range = pending_video_ycbcr_full_range_;
        const QSize overlay_size = video_surface_->size();
        const float subtitle_aspect = pending_video_display_aspect_ratio_;
        pending_hardware_texture_ = nullptr;
        pending_video_frame_valid_ = false;
        video_frame_pending_ = true;

        postBackground([this, owner = std::move(owner), texture, array_slice, format,
            width, height, generation, pts_ms, subtitle, embedded_subtitle_bitmap,
            render_subtitles, ycbcr_matrix, ycbcr_full_range, overlay_size,
            subtitle_aspect, font_family, subtitle_color, font_size, vertical_position,
            media_path]() mutable
        {
            QImage overlay;
            if (render_subtitles &&
                (!subtitle.isEmpty() || !embedded_subtitle_bitmap.isNull()))
            {
                overlay = QImage(overlay_size, QImage::Format_ARGB32);
                overlay.fill(Qt::transparent);
                if (!embedded_subtitle_bitmap.isNull())
                    draw_subtitle_bitmap(
                        overlay, embedded_subtitle_bitmap, true, subtitle_aspect);
                draw_subtitle_text(
                    overlay, subtitle, font_family, subtitle_color,
                    font_size, vertical_position);
            }

            postFrame([this, owner = std::move(owner), texture, array_slice, format,
                width, height, generation, pts_ms, subtitle, embedded_subtitle_bitmap,
                render_subtitles, ycbcr_matrix, ycbcr_full_range,
                font_family, subtitle_color, font_size, vertical_position,
                media_path, overlay = std::move(overlay)]() mutable
            {
                lnplayer_snapshot_t snapshot{};
                snapshot.struct_size = sizeof(snapshot);
                lnplayer_t* player = player_.load(std::memory_order_acquire);
                if (!player || lnplayer_get_snapshot(player, &snapshot) != LNPLAYER_RESULT_OK ||
                    snapshot.generation != generation)
                {
                    QMetaObject::invokeMethod(this, [this]
                    {
                        video_frame_pending_ = false;
                        if (is_playing_)
                            requestVideoFrame();
                    }, Qt::QueuedConnection);
                    return;
                }

                const auto fallback_owner = owner;
                const bool presented = video_surface_ &&
                    video_surface_->present_hardware_frame(
                        texture, array_slice, format, width, height,
                        std::move(owner), overlay);
                if (presented)
                {
                    QMetaObject::invokeMethod(this, [this]
                    {
                        video_frame_pending_ = false;
                        if (is_playing_)
                            requestVideoFrame();
                    }, Qt::QueuedConnection);
                    return;
                }

                struct FallbackFrame
                {
                    QImage image;
                } fallback;
                const lnplayer_result_t fallback_result =
                    lnplayer_windows_convert_video_frame(
                        fallback_owner.get(),
                        generation,
                        pts_ms,
                        ycbcr_matrix,
                        ycbcr_full_range,
                        +[](const lnplayer_windows_video_frame_t* frame, void* user_data)
                        {
                            auto* output = static_cast<FallbackFrame*>(user_data);
                            output->image = QImage(
                                frame->bgra,
                                frame->width,
                                frame->height,
                                frame->stride,
                                QImage::Format_ARGB32).copy();
                        },
                        &fallback);

                if (fallback_result == LNPLAYER_RESULT_OK && !fallback.image.isNull())
                {
                    if (render_subtitles && !embedded_subtitle_bitmap.isNull())
                        draw_subtitle_bitmap(fallback.image, embedded_subtitle_bitmap, false, 0.0f);
                    draw_subtitle_text(
                        fallback.image, subtitle, font_family, subtitle_color,
                        font_size, vertical_position);
                    if (video_surface_)
                        video_surface_->present_frame_threadsafe(fallback.image);
                    QMetaObject::invokeMethod(this,
                        [this, image = std::move(fallback.image), media_path]
                    {
                        video_frame_pending_ = false;
                        if (current_media_path_ == media_path)
                            last_video_frame_ = image;
                        if (is_playing_)
                            requestVideoFrame();
                    }, Qt::QueuedConnection);
                }
                else
                {
                    QMetaObject::invokeMethod(this, [this]
                    {
                        video_frame_pending_ = false;
                        status_label_->setText(
                            QStringLiteral("硬件视频帧渲染失败，软件回退也失败"));
                    }, Qt::QueuedConnection);
                }
            });
        });
        return;
    }

    QImage frame = pending_video_frame_;
    VideoFrameHistoryEntry history_entry;
    history_entry.image = frame;
    history_entry.hdr_transfer = pending_hdr_transfer_;
    history_entry.color_primaries = pending_color_primaries_;
    history_entry.hdr_peak_luminance = pending_hdr_peak_luminance_;
    history_entry.display_aspect_ratio = pending_video_display_aspect_ratio_;
    history_entry.ycbcr_matrix = pending_video_ycbcr_matrix_;
    history_entry.ycbcr_full_range = pending_video_ycbcr_full_range_;
    history_entry.embedded_subtitle = pending_embedded_subtitle_;
    history_entry.embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
    history_entry.pts_ms = pending_video_pts_ms_;
    history_entry.generation = pending_video_generation_;
    if (video_frame_history_index_ + 1 < video_frame_history_.size())
        video_frame_history_.erase(
            video_frame_history_.begin() + video_frame_history_index_ + 1,
            video_frame_history_.end());
    video_frame_history_.append(std::move(history_entry));
    trimVideoFrameHistory();
    video_frame_history_index_ = video_frame_history_.size() - 1;

    QString subtitle = subtitleAt(pending_video_pts_ms_);
    if (subtitles_enabled_ && !pending_embedded_subtitle_.isEmpty())
    {
        if (!subtitle.isEmpty())
            subtitle += QLatin1Char('\n');
        subtitle += pending_embedded_subtitle_;
    }
    const QImage embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
    const bool render_subtitles = subtitles_enabled_;
    const uint64_t generation = pending_video_generation_;
    const bool use_renderer = video_surface_->has_renderer();
    pending_video_frame_ = {};
    pending_video_frame_valid_ = false;
    video_frame_pending_ = true;

    postBackground([this, frame = std::move(frame), subtitle,
        embedded_subtitle_bitmap, render_subtitles, generation, media_path,
        font_family, subtitle_color, font_size, vertical_position, use_renderer]() mutable
    {
        if (render_subtitles && !embedded_subtitle_bitmap.isNull())
            draw_subtitle_bitmap(frame, embedded_subtitle_bitmap, false, 0.0f);
        draw_subtitle_text(
            frame, subtitle, font_family, subtitle_color, font_size, vertical_position);

        QMetaObject::invokeMethod(this,
            [this, frame = std::move(frame), generation, media_path, use_renderer]() mutable
        {
            if (current_media_path_ != media_path || generation != media_generation_ ||
                video_seek_pending_)
            {
                video_frame_pending_ = false;
                return;
            }

            last_video_frame_ = frame;
            if (use_renderer && video_surface_->has_renderer())
            {
                postFrame([this, frame = std::move(frame), generation, media_path]
                {
                    lnplayer_t* player = player_.load(std::memory_order_acquire);
                    lnplayer_snapshot_t snapshot{};
                    snapshot.struct_size = sizeof(snapshot);
                    if (!player || lnplayer_get_snapshot(player, &snapshot) != LNPLAYER_RESULT_OK ||
                        snapshot.generation != generation)
                    {
                        QMetaObject::invokeMethod(this, [this]
                        {
                            video_frame_pending_ = false;
                            if (is_playing_)
                                requestVideoFrame();
                        }, Qt::QueuedConnection);
                        return;
                    }

                    const bool presented = video_surface_ &&
                        video_surface_->present_frame_threadsafe(frame);
                    QMetaObject::invokeMethod(this,
                        [this, frame, presented, media_path]
                    {
                        video_frame_pending_ = false;
                        if (!presented && current_media_path_ == media_path &&
                            video_surface_ && !video_surface_->has_renderer())
                            video_surface_->set_frame(frame);
                        if (is_playing_)
                            requestVideoFrame();
                    }, Qt::QueuedConnection);
                });
            }
            else
            {
                video_surface_->set_frame(frame);
                video_frame_pending_ = false;
                if (is_playing_)
                    requestVideoFrame();
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::takeScreenshot()
{
    if (!video_surface_)
        return;

    VideoSurface* surface = video_surface_;
    QImage fallback = last_video_frame_;
    const QString initial_directory = screenshot_directory_.isEmpty()
        ? QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))
        : screenshot_directory_;
    const QString initial_format = screenshot_format_;

    postBackground([this, surface, fallback = std::move(fallback),
        initial_directory, initial_format]() mutable
    {
        QDir().mkpath(initial_directory);
        QImage screenshot = surface->capture_frame();
        if (screenshot.isNull())
            screenshot = std::move(fallback);

        QMetaObject::invokeMethod(this,
            [this, screenshot = std::move(screenshot), initial_directory, initial_format]() mutable
        {
            if (screenshot.isNull())
            {
                statusBar()->showMessage(QStringLiteral("当前没有可保存的视频画面"));
                return;
            }

            QString selected_filter = initial_format == QStringLiteral("jpg")
                ? QStringLiteral("JPEG 图片 (*.jpg *.jpeg)")
                : initial_format == QStringLiteral("bmp")
                    ? QStringLiteral("BMP 图片 (*.bmp)")
                    : QStringLiteral("PNG 图片 (*.png)");
            const QString path = QFileDialog::getSaveFileName(
                this,
                QStringLiteral("保存视频截图"),
                QDir(initial_directory).filePath(
                    QStringLiteral("screenshot.%1").arg(initial_format)),
                QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"),
                &selected_filter,
                QFileDialog::Options());
            if (path.isEmpty())
                return;

            QString output_format = initial_format;
            if (selected_filter.startsWith(QStringLiteral("JPEG")))
                output_format = QStringLiteral("jpg");
            else if (selected_filter.startsWith(QStringLiteral("BMP")))
                output_format = QStringLiteral("bmp");
            else
                output_format = QStringLiteral("png");

            QString output_path = path;
            const QString suffix = QFileInfo(output_path).suffix().toLower();
            if (suffix.isEmpty())
                output_path += QStringLiteral(".") + output_format;
            else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
                output_format = QStringLiteral("jpg");
            else if (suffix == QStringLiteral("bmp"))
                output_format = QStringLiteral("bmp");
            else if (suffix == QStringLiteral("png"))
                output_format = QStringLiteral("png");

            const QByteArray image_format = output_format.toLatin1();
            postBackground([this, screenshot = std::move(screenshot),
                output_path, output_format, image_format]() mutable
            {
                QDir().mkpath(QFileInfo(output_path).absolutePath());
                const bool saved = screenshot.save(output_path, image_format.constData());
                QMetaObject::invokeMethod(this,
                    [this, saved, output_path, output_format]
                {
                    if (!saved)
                    {
                        statusBar()->showMessage(QStringLiteral("截图保存失败"));
                        return;
                    }

                    screenshot_directory_ = QFileInfo(output_path).absolutePath();
                    screenshot_format_ = output_format;
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        QStringLiteral("Video/Screenshot/Directory"), screenshot_directory_);
                    settings.setValue(
                        QStringLiteral("Video/Screenshot/Format"), screenshot_format_);
                    settings.sync();
                    statusBar()->showMessage(
                        QStringLiteral("截图已保存：%1").arg(output_path));
                    showOsd(QStringLiteral("截图已保存"));
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

void MainWindow::stepVideoFrame(bool backwards)
{
    if (mode_ != PlayerMode::Video || !has_video_ || video_seek_pending_)
        return;

    if (is_playing_)
    {
        postCommand([this]
        {
            if (player_)
                lnplayer_pause(player_);
        });
        is_playing_ = false;
        video_timer_->stop();
    }

    if (video_frame_pending_)
        return;

    if (pending_video_frame_valid_)
    {
        presentPendingVideoFrame();
        if (video_frame_pending_)
            return;
    }

    if (backwards)
    {
        if (video_frame_history_index_ > 0)
        {
            --video_frame_history_index_;
            presentHistoricalVideoFrame();
        }
        return;
    }

    if (video_frame_history_index_ + 1 < video_frame_history_.size())
    {
        ++video_frame_history_index_;
        presentHistoricalVideoFrame();
        return;
    }

    requestVideoFrame();
}

void MainWindow::trimVideoFrameHistory()
{
    constexpr qsizetype maximum_software_history_bytes = 256 * 1024 * 1024;
    constexpr int maximum_history_frames = 120;
    constexpr int maximum_hardware_history_frames = 8;

    qsizetype software_bytes = 0;
    int hardware_frames = 0;
    for (const VideoFrameHistoryEntry& entry : video_frame_history_)
    {
        if (entry.hardware_owner)
            ++hardware_frames;
        else if (!entry.image.isNull())
            software_bytes += entry.image.sizeInBytes();
    }

    while (!video_frame_history_.isEmpty() &&
        (video_frame_history_.size() > maximum_history_frames ||
            software_bytes > maximum_software_history_bytes ||
            hardware_frames > maximum_hardware_history_frames))
    {
        const VideoFrameHistoryEntry& oldest = video_frame_history_.first();
        if (oldest.hardware_owner)
            --hardware_frames;
        else if (!oldest.image.isNull())
            software_bytes -= oldest.image.sizeInBytes();
        video_frame_history_.removeFirst();
        if (video_frame_history_index_ >= 0)
            --video_frame_history_index_;
    }

    if (video_frame_history_.isEmpty())
        video_frame_history_index_ = -1;
}

void MainWindow::presentHistoricalVideoFrame()
{
    if (!video_surface_ ||
        video_frame_history_index_ < 0 ||
        video_frame_history_index_ >= video_frame_history_.size())
        return;

    const VideoFrameHistoryEntry entry = video_frame_history_[video_frame_history_index_];
    viewing_historical_frame_ = true;
    current_position_ms_ = entry.pts_ms;
    const QString media_path = current_media_path_;
    const QString duration_text = duration_ms_ < 0
        ? QStringLiteral("--:--")
        : format_position(duration_ms_);
    position_label_->setText(
        QStringLiteral("%1 / %2").arg(format_position(entry.pts_ms), duration_text));
    if (!slider_dragging_ && duration_ms_ > 0)
        seek_slider_->setValue(static_cast<int>(
            std::clamp<int64_t>(entry.pts_ms * 1000 / duration_ms_, 0, 1000)));

    video_surface_->set_hdr_metadata(
        entry.hdr_transfer, entry.color_primaries, entry.hdr_peak_luminance);
    video_surface_->set_video_color_space(entry.ycbcr_matrix, entry.ycbcr_full_range);
    video_surface_->set_source_aspect_ratio(entry.display_aspect_ratio);

    QString subtitle = subtitleAt(entry.pts_ms);
    if (subtitles_enabled_ && !entry.embedded_subtitle.isEmpty())
    {
        if (!subtitle.isEmpty())
            subtitle += QLatin1Char('\n');
        subtitle += entry.embedded_subtitle;
    }

    const QString font_family = subtitle_font_family_;
    const QString subtitle_color = subtitle_color_;
    const int font_size = subtitle_font_size_;
    const int vertical_position = subtitle_vertical_position_;
    const bool render_subtitles = subtitles_enabled_;
    video_frame_pending_ = true;

    if (entry.hardware_owner)
    {
        const QSize overlay_size = video_surface_->size();
        postBackground([this, entry, overlay_size, subtitle, render_subtitles,
            font_family, subtitle_color, font_size, vertical_position]
        {
            QImage overlay(overlay_size, QImage::Format_ARGB32);
            overlay.fill(Qt::transparent);
            if (render_subtitles && !entry.embedded_subtitle_bitmap.isNull())
                draw_subtitle_bitmap(
                    overlay, entry.embedded_subtitle_bitmap, true, entry.display_aspect_ratio);
            draw_subtitle_text(
                overlay, subtitle, font_family, subtitle_color, font_size, vertical_position);

            postCommand([this, entry, overlay = std::move(overlay)]() mutable
            {
                lnplayer_t* player = player_.load(std::memory_order_acquire);
                lnplayer_snapshot_t snapshot{};
                snapshot.struct_size = sizeof(snapshot);
                const bool current = player &&
                    lnplayer_get_snapshot(player, &snapshot) == LNPLAYER_RESULT_OK &&
                    snapshot.generation == entry.generation;
                const bool presented = current && video_surface_ &&
                    video_surface_->present_hardware_frame(
                        entry.hardware_texture,
                        entry.hardware_array_slice,
                        entry.hardware_format,
                        entry.hardware_width,
                        entry.hardware_height,
                        entry.hardware_owner,
                        overlay);
                QMetaObject::invokeMethod(this, [this, presented, current]
                {
                    video_frame_pending_ = false;
                    if (current && !presented)
                        status_label_->setText(
                            QStringLiteral("无法重新呈现历史硬件视频帧"));
                }, Qt::QueuedConnection);
            });
        });
        return;
    }

    QImage frame = entry.image;
    postBackground([this, frame = std::move(frame), entry, subtitle,
        render_subtitles, font_family, subtitle_color, font_size,
        vertical_position, media_path]() mutable
    {
        if (render_subtitles && !entry.embedded_subtitle_bitmap.isNull())
            draw_subtitle_bitmap(frame, entry.embedded_subtitle_bitmap, false, 0.0f);
        draw_subtitle_text(
            frame, subtitle, font_family, subtitle_color, font_size, vertical_position);

        QMetaObject::invokeMethod(this,
            [this, frame = std::move(frame), entry, media_path]() mutable
        {
            video_frame_pending_ = false;
            if (current_media_path_ != media_path || entry.generation != media_generation_)
                return;
            last_video_frame_ = frame;
            video_surface_->set_frame(frame);
        }, Qt::QueuedConnection);
    });
}
