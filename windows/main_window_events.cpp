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
}

void MainWindow::showPlaybackInfo()
{
    if (mode_ != PlayerMode::Video)
        return;

    const QString media_path = playlist_index_ >= 0 && playlist_index_ < playlist_.size()
        ? playlist_[playlist_index_].toLocalFile()
        : QString();
    const QString duration = duration_ms_ >= 0
        ? format_position(duration_ms_)
        : QStringLiteral("--:--");
    const QString position = format_position(current_position_ms_);
    const QString video_state = has_video_
        ? QStringLiteral("有视频流")
        : QStringLiteral("无视频流");
    const QString audio_state = has_audio_
        ? QStringLiteral("有音轨")
        : QStringLiteral("无音轨");
    const QFileInfo media_file_info(media_path);
    const QString file_size = media_file_info.exists()
        ? QLocale().formattedDataSize(media_file_info.size())
        : QStringLiteral("未知");
    const QString resolution = video_width_ > 0 && video_height_ > 0
        ? QStringLiteral("%1 × %2").arg(video_width_).arg(video_height_)
        : QStringLiteral("未知");
    const QString renderer_state = video_surface_ && video_surface_->has_renderer()
        ? QStringLiteral("D3D11 渲染器已初始化")
        : QStringLiteral("D3D11 渲染器不可用");
    const double frame_rate = video_info_.frame_rate_den > 0
        ? static_cast<double>(video_info_.frame_rate_num) / video_info_.frame_rate_den
        : 0.0;
    const QString metadata = QStringLiteral(
        "容器：%1\n编码：%2（%3）\n解码器：%4\n帧率：%5\n码率：%6\n"
        "像素格式：%7 / %8-bit\n硬件解码：%9\nSAR：%10:%11\n旋转：%12°\n"
        "HDR：Mastering=%17/%18 nit MaxCLL=%19 MaxFALL=%20\n"
        "色彩：range=%13 primaries=%14 transfer=%15 matrix=%16")
        .arg(QString::fromUtf8(video_info_.container_long_name))
        .arg(QString::fromUtf8(video_info_.codec_name))
        .arg(QString::fromUtf8(video_info_.profile))
        .arg(QString::fromUtf8(video_info_.decoder_name))
        .arg(frame_rate > 0.0
            ? QString::number(frame_rate, 'f', 3) + QStringLiteral(" fps")
            : QStringLiteral("未知"))
        .arg(video_info_.bit_rate > 0
            ? QString::number(video_info_.bit_rate / 1000) + QStringLiteral(" kbps")
            : QStringLiteral("未知"))
        .arg(QString::fromUtf8(video_info_.pixel_format))
        .arg(video_info_.bit_depth)
        .arg(video_info_.hardware_decoding
            ? QStringLiteral("D3D11VA") : QStringLiteral("软件/未启用"))
        .arg(video_info_.sample_aspect_ratio_num)
        .arg(video_info_.sample_aspect_ratio_den)
        .arg(video_info_.rotation_degrees)
        .arg(video_info_.color_range)
        .arg(video_info_.color_primaries)
        .arg(video_info_.color_transfer)
        .arg(video_info_.color_space)
        .arg(video_info_.mastering_max_luminance, 0, 'f', 2)
        .arg(video_info_.mastering_min_luminance, 0, 'f', 4)
        .arg(video_info_.max_content_light_level)
        .arg(video_info_.max_frame_average_light_level);

    const QString details = QStringLiteral(
        "媒体：%1\n路径：%2\n大小：%14\n状态：%3\n位置：%4 / %5\n"
        "视频：%6\n分辨率：%7\n音轨：%8\n播放速度：%9x\n音量：%10%\n"
        "字幕偏移：%11 ms\n渲染：%12\n\n%13")
        .arg(media_title_label_->text())
        .arg(media_path.isEmpty() ? QStringLiteral("无") : media_path)
        .arg(status_label_->text())
        .arg(position)
        .arg(duration)
        .arg(video_state)
        .arg(resolution)
        .arg(audio_state)
        .arg(playback_rate_, 0, 'f', 2)
        .arg(volume_slider_->value() / 10)
        .arg(subtitle_offset_ms_)
        .arg(renderer_state)
        .arg(metadata)
        .arg(file_size);
    const bool fullscreen_before_dialog = isFullScreen();
    if (fullscreen_before_dialog)
        fullscreen_hide_timer_->stop();
    QMessageBox::information(this, QStringLiteral("播放信息"), details);
    if (fullscreen_before_dialog && isFullScreen())
}

void MainWindow::positionOsd()
{
    if (!osd_label_ || !video_surface_)
        return;

    osd_label_->adjustSize();
    const int x = (video_surface_->width() - osd_label_->width()) / 2;
    const int y = osd_position_ == 0
        ? video_surface_->height() / 6
        : osd_position_ == 2
            ? video_surface_->height() * 5 / 6 - osd_label_->height()
            : (video_surface_->height() - osd_label_->height()) / 2;
    osd_label_->move(x, std::clamp(y, 0, std::max(0, video_surface_->height() - osd_label_->height())));
}

void MainWindow::showOsd(const QString& message)
{
    if (!osd_label_ || !video_surface_ || message.isEmpty())
        return;

    osd_label_->setText(message);
    positionOsd();
    osd_label_->raise();
    osd_label_->show();
    osd_timer_->start(osd_duration_ms_);
}

void MainWindow::fitWindowToVideoAspect(
    float aspect_ratio,
    const QSize& source_size)
{
    if (!video_surface_ || aspect_ratio <= 0.0f ||
        source_size.width() <= 0 || source_size.height() <= 0 ||
        isMaximized() || isFullScreen() || video_surface_->width() <= 0 ||
        video_surface_->height() <= 0)
        return;

    QScreen* target_screen = screen();
    if (!target_screen)
        return;

    // Start at the decoded frame's native pixel dimensions. Account for
    // rotation metadata when the display aspect is portrait/landscape while
    // the coded frame has the opposite orientation.
    int target_surface_width = source_size.width();
    int target_surface_height = source_size.height();
    const bool source_is_landscape = target_surface_width > target_surface_height;
    const bool display_is_landscape = aspect_ratio >= 1.0f;
    if (source_is_landscape != display_is_landscape)
        std::swap(target_surface_width, target_surface_height);

    // Preserve the display aspect ratio (including sample aspect ratio) while
    // keeping one dimension at native resolution whenever possible.
    if (target_surface_width / static_cast<float>(target_surface_height) > aspect_ratio)
        target_surface_width = qRound(target_surface_height * aspect_ratio);
    else
        target_surface_height = qRound(target_surface_width / aspect_ratio);

    const int window_chrome_width = width() - video_surface_->width();
    const int window_chrome_height = height() - video_surface_->height();
    const QRect available = target_screen->availableGeometry();
    const int maximum_window_width = std::max(1, available.width() - 24);
    const int maximum_window_height = std::max(1, available.height() - 48);

    int target_window_width = target_surface_width + window_chrome_width;
    int target_window_height = target_surface_height + window_chrome_height;

    // If native size does not fit, scale the video uniformly to the largest
    // available client area. Never enlarge a smaller source frame.
    const float scale = std::min({
        1.0f,
        maximum_window_width / static_cast<float>(target_window_width),
        maximum_window_height / static_cast<float>(target_window_height)
    });
    target_surface_width = std::max(1, qRound(target_surface_width * scale));
    target_surface_height = std::max(1, qRound(target_surface_height * scale));
    target_window_width = target_surface_width + window_chrome_width;
    target_window_height = target_surface_height + window_chrome_height;

    if (std::abs(target_window_width - width()) >= 8 ||
        std::abs(target_window_height - height()) >= 8)
        resize(target_window_width, target_window_height);

    const int maximum_x = available.right() + 1 - target_window_width;
    const int maximum_y = available.bottom() + 1 - target_window_height;
    QPoint adjusted_position = pos();
    adjusted_position.setX(std::clamp(
        adjusted_position.x(), available.left(), std::max(available.left(), maximum_x)));
    adjusted_position.setY(std::clamp(
        adjusted_position.y(), available.top(), std::max(available.top(), maximum_y)));
    if (adjusted_position != pos())
        move(adjusted_position);
}

void MainWindow::updateFullscreenUi()
{
    fullscreen_hide_timer_->stop();

    if (isFullScreen())
    {
        if (menu_bar_)
            menu_bar_->hide();
        if (title_bar_)
            title_bar_->hide();
        if (statusBar())
            statusBar()->hide();
    }
    else
    {
        unsetCursor();
        if (title_bar_)
            title_bar_->hide();
        if (statusBar())
            statusBar()->setVisible(mode_ == PlayerMode::Audio);
    }

    if (title_bar_)
        title_bar_->hide();
    if (playlist_panel_)
        playlist_panel_->hide();
    unsetCursor();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == playlist_widget_ && event->type() == QEvent::DragEnter)
    {
        auto* drag_event = static_cast<QDragEnterEvent*>(event);
        if (drag_event->mimeData()->hasUrls())
        {
            drag_event->acceptProposedAction();
            return true;
        }
    }
    if (watched == playlist_widget_ && event->type() == QEvent::Drop)
    {
        auto* drop_event = static_cast<QDropEvent*>(event);
        if (drop_event->mimeData()->hasUrls())
        {
            appendPlaylistFiles(drop_event->mimeData()->urls());
            drop_event->acceptProposedAction();
            return true;
        }
    }

    if (watched == seek_slider_ && event->type() == QEvent::MouseMove &&
        duration_ms_ > 0)
    {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        const int handle_width = style()->pixelMetric(
            QStyle::PM_SliderLength, nullptr, seek_slider_);
        const int margin = handle_width / 2;
        const int span = std::max(1, seek_slider_->width() - handle_width);
        const int position = std::clamp(
            mouse_event->position().toPoint().x() - margin, 0, span);
        const int value = QStyle::sliderValueFromPosition(
            seek_slider_->minimum(), seek_slider_->maximum(),
            position, span, seek_slider_->invertedAppearance());
        seek_slider_->setToolTip(format_position(duration_ms_ * value / 1000));
    }

    if (mode_ == PlayerMode::Video)
    {
        QWidget* watched_widget = qobject_cast<QWidget*>(watched);
        bool is_menu_widget = qobject_cast<QMenu*>(watched) != nullptr;
        for (QWidget* parent = watched_widget ? watched_widget->parentWidget() : nullptr;
            parent && !is_menu_widget; parent = parent->parentWidget())
            is_menu_widget = qobject_cast<QMenu*>(parent) != nullptr;
        const bool is_control_widget = watched == control_panel_ ||
            (control_panel_ && watched_widget && control_panel_->isAncestorOf(watched_widget)) ||
            watched == playlist_panel_ ||
            (playlist_panel_ && watched_widget && playlist_panel_->isAncestorOf(watched_widget)) ||
            watched == menu_bar_ || (menu_bar_ && watched_widget && menu_bar_->isAncestorOf(watched_widget)) ||
            watched == title_bar_ || (title_bar_ && watched_widget && title_bar_->isAncestorOf(watched_widget)) ||
            watched == statusBar() || (statusBar() && watched_widget && statusBar()->isAncestorOf(watched_widget)) ||
            is_menu_widget;
        if (watched == video_surface_ && event->type() == QEvent::Resize &&
            empty_state_label_ && empty_state_label_->isVisible())
        {
            empty_state_label_->adjustSize();
            empty_state_label_->move(
                (video_surface_->width() - empty_state_label_->width()) / 2,
                (video_surface_->height() - empty_state_label_->height()) / 2);
        }
        if (watched == video_surface_ && event->type() == QEvent::Resize)
        {
            if (osd_label_ && osd_label_->isVisible())
                positionOsd();
            if (playlist_panel_ && playlist_panel_->isVisible())
            {
                playlist_panel_->setGeometry(
                    std::max(0, video_surface_->width() - playlist_panel_->width()),
                    0, playlist_panel_->width(), video_surface_->height());
                playlist_panel_->raise();
            }
        }
        if (event->type() == QEvent::Show && is_menu_widget)
        {
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Hide && is_menu_widget)
        {
            if (!QApplication::activePopupWidget() && fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
        else if (event->type() == QEvent::MouseMove)
        {
            if (is_control_widget)
            {
                unsetCursor();
                fullscreen_hide_timer_->stop();
            }
            else if (watched == video_surface_)
            {
                const QPoint position = static_cast<QMouseEvent*>(event)->position().toPoint();
                constexpr int edge_height = 8;
                if (position.y() <= edge_height && title_bar_)
                {
                    unsetCursor();
                    title_bar_->setGeometry(0, 0, centralWidget()->width(),
                        title_bar_->sizeHint().height());
                    title_bar_->show();
                    title_bar_->raise();
                    if (fullscreen_hide_delay_ms_ > 0)
                        fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
                    else
                        fullscreen_hide_timer_->stop();
                }
                else if (playlist_panel_visible_ && playlist_panel_ &&
                    position.x() >= video_surface_->width() - edge_height)
                {
                    unsetCursor();
                    playlist_panel_->setGeometry(
                        std::max(0, video_surface_->width() - playlist_panel_->width()),
                        0, playlist_panel_->width(), video_surface_->height());
                    playlist_panel_->show();
                    playlist_panel_->raise();
                    if (fullscreen_hide_delay_ms_ > 0)
                        fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
                    else
                        fullscreen_hide_timer_->stop();
                }
            }
        }
        else if (event->type() == QEvent::Enter && is_control_widget)
        {
            unsetCursor();
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Leave && is_control_widget)
        {
            QWidget* cursor_widget = QApplication::widgetAt(QCursor::pos());
            const bool pointer_over_controls = cursor_widget &&
                ((control_panel_ &&
                    (cursor_widget == control_panel_ || control_panel_->isAncestorOf(cursor_widget))) ||
                 (playlist_panel_ &&
                    (cursor_widget == playlist_panel_ || playlist_panel_->isAncestorOf(cursor_widget))) ||
                 (menu_bar_ &&
                    (cursor_widget == menu_bar_ || menu_bar_->isAncestorOf(cursor_widget))) ||
                 (title_bar_ &&
                    (cursor_widget == title_bar_ || title_bar_->isAncestorOf(cursor_widget))) ||
                 (statusBar() &&
                    (cursor_widget == statusBar() || statusBar()->isAncestorOf(cursor_widget))));
            if (!pointer_over_controls && !QApplication::activePopupWidget() &&
                fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
    {
        if (!isFullScreen() && !isMinimized())
            normal_geometry_ = saveGeometry();
        if (title_bar_)
        {
            if (auto* maximize_button = title_bar_->findChild<QPushButton*>(
                QStringLiteral("maximizeWindowButton")))
            {
                maximize_button->setIcon(PlayerIcons::make(
                    isMaximized() ? PlayerIcons::Name::Restore : PlayerIcons::Name::Maximize,
                    QColor(QStringLiteral("#9aa6b5"))));
                maximize_button->setIconSize(QSize(15, 15));
                maximize_button->setToolTip(isMaximized()
                    ? QStringLiteral("还原") : QStringLiteral("最大化"));
                maximize_button->setAccessibleName(maximize_button->toolTip());
            }
        }
        updateFullscreenUi();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (mode_ == PlayerMode::Video)
    {
        if (playlist_panel_ && playlist_panel_->isVisible())
        {
            playlist_panel_->setGeometry(
                std::max(0, video_surface_->width() - playlist_panel_->width()),
                0, playlist_panel_->width(), video_surface_->height());
            playlist_panel_->raise();
        }
        if (title_bar_)
        {
            title_bar_->setGeometry(0, 0, centralWidget()->width(),
                title_bar_->sizeHint().height());
            if (title_bar_->isVisible())
                title_bar_->raise();
        }
    }
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
}

bool MainWindow::nativeEvent(
    const QByteArray& event_type, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    auto* native_message = static_cast<MSG*>(message);
    if (mode_ == PlayerMode::Video &&
        native_message->message == WM_NCHITTEST && !isFullScreen())
    {
        const QPoint position = mapFromGlobal(QCursor::pos());
        if (!isMaximized())
        {
            constexpr int border = 12;
            const bool left = position.x() < border;
            const bool right = position.x() >= width() - border;
            const bool top = position.y() < border;
            const bool bottom = position.y() >= height() - border;
            if (left || right || top || bottom)
            {
                if (top && left)
                    *result = HTTOPLEFT;
                else if (top && right)
                    *result = HTTOPRIGHT;
                else if (bottom && left)
                    *result = HTBOTTOMLEFT;
                else if (bottom && right)
                    *result = HTBOTTOMRIGHT;
                else if (left)
                    *result = HTLEFT;
                else if (right)
                    *result = HTRIGHT;
                else if (top)
                    *result = HTTOP;
                else
                    *result = HTBOTTOM;
                return true;
            }
        }
        if (title_bar_ && title_bar_->isVisible() &&
            title_bar_->geometry().contains(position))
        {
            for (QWidget* child = childAt(position);
                child && child != title_bar_; child = child->parentWidget())
            {
                if (qobject_cast<QAbstractButton*>(child))
                    return QMainWindow::nativeEvent(event_type, message, result);
            }
            *result = HTCAPTION;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(event_type, message, result);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    openFiles(event->mimeData()->urls());
    event->acceptProposedAction();
}
