#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"
#include "d3d11_video_renderer.h"
#include "lnplayer_windows.h"

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QByteArray>
#include <QFont>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QMetaObject>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QStringDecoder>
#include <QSlider>
#include <QStatusBar>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>

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

VideoSurface::VideoSurface(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 360);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background-color: #101010;"));

    renderer_ = new lnplayer::windows::D3D11VideoRenderer();
    const HWND window = reinterpret_cast<HWND>(static_cast<quintptr>(winId()));
    if (!renderer_->initialize(window))
    {
        delete renderer_;
        renderer_ = nullptr;
    }
}

VideoSurface::~VideoSurface()
{
    delete renderer_;
    renderer_ = nullptr;
}

void VideoSurface::set_frame(const QImage& frame)
{
    frame_ = frame;
    if (renderer_)
    {
        if (frame_.isNull())
            renderer_->clear();
        else
            renderer_->present(
                frame_.constBits(),
                frame_.width(),
                frame_.height(),
                frame_.bytesPerLine());
        return;
    }
    update();
}

bool VideoSurface::has_renderer() const
{
    return renderer_ != nullptr;
}

void* VideoSurface::native_device() const
{
    return renderer_ ? renderer_->native_device() : nullptr;
}

void VideoSurface::set_stretch(bool enabled)
{
    stretch_ = enabled;
    if (renderer_)
        renderer_->set_stretch(enabled);
    else
        update();
}

void VideoSurface::set_aspect_ratio(float ratio)
{
    aspect_ratio_ = ratio;
    stretch_ = false;
    if (renderer_)
        renderer_->set_aspect_ratio(ratio > 0.0f ? ratio : source_aspect_ratio_);
    else
        update();
}

void VideoSurface::set_source_aspect_ratio(float ratio)
{
    source_aspect_ratio_ = ratio;
    if (aspect_ratio_ <= 0.0f && renderer_)
        renderer_->set_aspect_ratio(source_aspect_ratio_);
    else
        update();
}

void VideoSurface::set_video_filter(
    float brightness, float contrast, float saturation, float hue_degrees,
    float sharpness, float denoise, bool grayscale)
{
    if (renderer_)
        renderer_->set_video_filter(
            brightness, contrast, saturation, hue_degrees, sharpness, denoise, grayscale);
}

void VideoSurface::set_hdr_metadata(int transfer, int color_primaries)
{
    if (renderer_)
        renderer_->set_hdr_metadata(transfer, color_primaries);
}

void VideoSurface::set_video_color_space(int ycbcr_matrix, int full_range)
{
    if (renderer_)
        renderer_->set_video_color_space(ycbcr_matrix, full_range);
}

bool VideoSurface::present_frame_threadsafe(const QImage& frame)
{
    return renderer_ && !frame.isNull() && renderer_->present(
        frame.constBits(),
        frame.width(),
        frame.height(),
        frame.bytesPerLine());
}

bool VideoSurface::present_hardware_frame(
    void* texture, uint32_t array_slice, uint32_t format,
    int width, int height, std::shared_ptr<void> frame_owner,
    const QImage& overlay)
{
    return renderer_ && renderer_->present_hardware(
        texture, array_slice, format, width, height, std::move(frame_owner),
        overlay.isNull() ? nullptr : overlay.constBits(),
        overlay.isNull() ? 0 : overlay.bytesPerLine());
}

QImage VideoSurface::capture_frame() const
{
    if (!renderer_)
        return frame_;
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    if (!renderer_->capture_bgra(pixels, width, height))
        return frame_;
    return QImage(
        pixels.data(), width, height, width * 4, QImage::Format_ARGB32).copy();
}

void VideoSurface::set_interaction_callbacks(
    std::function<void()> double_click,
    std::function<void(int)> wheel)
{
    double_click_callback_ = std::move(double_click);
    wheel_callback_ = std::move(wheel);
}

void VideoSurface::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && double_click_callback_)
        double_click_callback_();
    QWidget::mouseDoubleClickEvent(event);
}

void VideoSurface::wheelEvent(QWheelEvent* event)
{
    if (wheel_callback_ && event->angleDelta().y() != 0)
        wheel_callback_(event->angleDelta().y() > 0 ? 1 : -1);
    event->accept();
}

void VideoSurface::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (renderer_)
        return;

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame_.isNull())
        return;

    const float display_aspect = aspect_ratio_ > 0.0f
        ? aspect_ratio_
        : source_aspect_ratio_;
    const QSize source_size = display_aspect > 0.0f
        ? QSize(static_cast<int>(frame_.height() * display_aspect), frame_.height())
        : frame_.size();
    const QSize fitted = source_size.scaled(
        size(), stretch_ ? Qt::IgnoreAspectRatio : Qt::KeepAspectRatio);
    const QRect target(
        (width() - fitted.width()) / 2,
        (height() - fitted.height()) / 2,
        fitted.width(),
        fitted.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, frame_);
}

void VideoSurface::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (renderer_)
        renderer_->resize(
            static_cast<uint32_t>(event->size().width()),
            static_cast<uint32_t>(event->size().height()));
}

namespace
{

QString format_position(int64_t position_ms)
{
    const int64_t total_seconds = position_ms < 0 ? 0 : position_ms / 1000;
    const int64_t minutes = total_seconds / 60;
    const int64_t seconds = total_seconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

}

MainWindow::MainWindow(
    PlayerMode mode, const QStringList& initial_media_files, QWidget* parent)
    : QMainWindow(parent)
    , mode_(mode)
    , status_label_(new QLabel(this))
    , position_label_(new QLabel(QStringLiteral("00:00 / --:--"), this))
    , media_title_label_(new QLabel(QStringLiteral("未打开媒体"), this))
    , library_search_(new QLineEdit(this))
    , library_list_(new QListWidget(this))
    , seek_slider_(new QSlider(Qt::Horizontal, this))
    , volume_slider_(new QSlider(Qt::Horizontal, this))
    , play_button_(new QPushButton(QStringLiteral("播放"), this))
    , pause_button_(new QPushButton(QStringLiteral("暂停"), this))
    , stop_button_(new QPushButton(QStringLiteral("停止"), this))
    , command_worker_(new QObject())
    , command_thread_(new QThread(this))
    , library_worker_(new QObject())
    , library_thread_(new QThread(this))
    , snapshot_timer_(new QTimer(this))
    , video_timer_(new QTimer(this))
{
    setAcceptDrops(true);
    resize(mode_ == PlayerMode::Video ? QSize(960, 600) : QSize(720, 480));
    setWindowTitle(mode_ == PlayerMode::Video
        ? QStringLiteral("LNPlayer - Video")
        : QStringLiteral("LNPlayer - Audio"));

    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* open_action = file_menu->addAction(QStringLiteral("打开文件..."));
    connect(open_action, &QAction::triggered, this, [this]
    {
        openFile();
    });
    if (mode_ == PlayerMode::Video)
    {
        auto* subtitle_action = file_menu->addAction(QStringLiteral("加载外挂字幕..."));
        connect(subtitle_action, &QAction::triggered, this, [this]
        {
            loadExternalSubtitle();
        });
    }
    if (mode_ == PlayerMode::Audio)
    {
        auto* import_action = file_menu->addAction(QStringLiteral("导入音乐目录..."));
        connect(import_action, &QAction::triggered, this, [this]
        {
            const QString directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择音乐目录"));
            if (directory.isEmpty())
                return;

            lnplayer_t* player_handle = nullptr;
            QMetaObject::invokeMethod(command_worker_, [this, &player_handle]
            {
                player_handle = player_;
            }, Qt::BlockingQueuedConnection);

            if (!player_handle)
                return;

            const QByteArray path_utf8 = directory.toUtf8();
            status_label_->setText(QStringLiteral("正在扫描音乐目录..."));
            QMetaObject::invokeMethod(library_worker_, [this, path_utf8, player_handle]
            {
                int imported_count = 0;
                const lnplayer_result_t result = lnplayer_library_scan_directory(
                    player_handle, path_utf8.constData(), 1, &imported_count);

                QMetaObject::invokeMethod(this, [this, result, imported_count]
                {
                    if (result == LNPLAYER_RESULT_OK)
                    {
                        status_label_->setText(
                            QStringLiteral("目录扫描完成，导入 %1 个音频文件").arg(imported_count));
                        statusBar()->showMessage(QStringLiteral("音乐库已更新"));
                        refreshLibrary();
                    }
                    else
                    {
                        status_label_->setText(QStringLiteral("音乐目录扫描失败"));
                    }
                }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        });
    }

    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QStringLiteral("退出"));
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);

    if (mode_ == PlayerMode::Video)
    {
        video_surface_ = new VideoSurface(content);
        playlist_widget_ = new QListWidget(content);
        playlist_widget_->setMaximumWidth(280);
        playlist_widget_->setDragDropMode(QAbstractItemView::InternalMove);
        playlist_widget_->setDefaultDropAction(Qt::MoveAction);
        auto* playlist_panel = new QWidget(content);
        auto* playlist_panel_layout = new QVBoxLayout(playlist_panel);
        playlist_panel_layout->setContentsMargins(0, 0, 0, 0);
        auto* playlist_actions = new QHBoxLayout();
        auto* add_to_queue = new QPushButton(QStringLiteral("添加"), playlist_panel);
        auto* remove_from_queue = new QPushButton(QStringLiteral("移除"), playlist_panel);
        auto* move_up = new QPushButton(QStringLiteral("上移"), playlist_panel);
        auto* move_down = new QPushButton(QStringLiteral("下移"), playlist_panel);
        auto* shuffle_button = new QPushButton(QStringLiteral("随机：关"), playlist_panel);
        auto* repeat_button = new QPushButton(QStringLiteral("循环：关"), playlist_panel);
        shuffle_button_ = shuffle_button;
        repeat_button_ = repeat_button;
        shuffle_button->setCheckable(true);
        playlist_actions->addWidget(add_to_queue);
        playlist_actions->addWidget(remove_from_queue);
        playlist_actions->addWidget(move_up);
        playlist_actions->addWidget(move_down);
        playlist_panel_layout->addLayout(playlist_actions);
        auto* playback_modes = new QHBoxLayout();
        playback_modes->addWidget(shuffle_button);
        playback_modes->addWidget(repeat_button);
        playlist_panel_layout->addLayout(playback_modes);
        playlist_panel_layout->addWidget(playlist_widget_, 1);
        connect(add_to_queue, &QPushButton::clicked, this, [this]
        {
            const QStringList files = QFileDialog::getOpenFileNames(
                this, QStringLiteral("添加到播放队列"), QString(),
                QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg);;所有文件 (*)"));
            QList<QUrl> urls;
            for (const QString& file : files)
                urls.append(QUrl::fromLocalFile(file));
            appendPlaylistFiles(urls);
        });
        connect(remove_from_queue, &QPushButton::clicked, this,
            [this] { removeSelectedPlaylistItem(); });
        connect(move_up, &QPushButton::clicked, this,
            [this] { moveSelectedPlaylistItem(-1); });
        connect(move_down, &QPushButton::clicked, this,
            [this] { moveSelectedPlaylistItem(1); });
        connect(shuffle_button, &QPushButton::toggled, this, [this, shuffle_button](bool enabled)
        {
            shuffle_enabled_ = enabled;
            resetShuffleOrder();
            shuffle_button->setText(enabled ? QStringLiteral("随机：开") : QStringLiteral("随机：关"));
            saveVideoPlaylist();
        });
        connect(repeat_button, &QPushButton::clicked, this, [this, repeat_button]
        {
            cycleRepeatMode();
            repeat_button->setText(repeat_mode_ == 0 ? QStringLiteral("循环：关")
                : repeat_mode_ == 1 ? QStringLiteral("循环：列表") : QStringLiteral("循环：单项"));
            saveVideoPlaylist();
        });
        connect(playlist_widget_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int)
            {
                QList<QUrl> reordered;
                for (int row = 0; row < playlist_widget_->count(); ++row)
                {
                    const QString path = playlist_widget_->item(row)->data(Qt::UserRole).toString();
                    reordered.append(QUrl::fromLocalFile(path));
                }
                playlist_ = std::move(reordered);
                playlist_index_ = playing_playlist_item_
                    ? playlist_widget_->row(playing_playlist_item_)
                    : -1;
                if (playlist_index_ >= 0)
                    playlist_widget_->setCurrentRow(playlist_index_);
                resetShuffleOrder();
            });
        connect(playlist_widget_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item)
            {
                openPlaylistIndex(playlist_widget_->row(item));
            });
        video_surface_->set_interaction_callbacks(
            [this]
            {
                if (isFullScreen())
                    showNormal();
                else
                    showFullScreen();
            },
            [this](int direction)
            {
                volume_slider_->setValue(std::clamp(
                    volume_slider_->value() + direction * 50, 0, 1000));
            });
        video_surface_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(video_surface_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point)
        {
            QMenu menu(this);
            QAction* play_pause = menu.addAction(is_playing_
                ? QStringLiteral("暂停")
                : QStringLiteral("播放"));
            QAction* stop = menu.addAction(QStringLiteral("停止"));
            menu.addSeparator();
            QAction* fullscreen = menu.addAction(isFullScreen()
                ? QStringLiteral("退出全屏")
                : QStringLiteral("全屏"));
            QAction* screenshot = menu.addAction(QStringLiteral("截图..."));

            QMenu* audio_track_menu = menu.addMenu(QStringLiteral("音轨"));
            struct AudioTrackMenuEntry
            {
                int stream_index;
                QString label;
                bool is_selected;
            };
            std::vector<AudioTrackMenuEntry> audio_tracks;
            if (player_)
            {
                lnplayer_enumerate_audio_tracks(
                    player_,
                    [](const lnplayer_audio_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<AudioTrackMenuEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Audio");
                        if (track->channels > 0)
                            label += QStringLiteral(" (%1 ch)").arg(track->channels);
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        entries->push_back({track->stream_index, label, track->is_selected != 0});
                    },
                    &audio_tracks);
            }
            for (const auto& track : audio_tracks)
            {
                QAction* action = audio_track_menu->addAction(track.label);
                action->setCheckable(true);
                action->setChecked(track.is_selected);
                action->setData(track.stream_index);
            }
            if (audio_tracks.empty())
            {
                QAction* unavailable = audio_track_menu->addAction(QStringLiteral("无可用音轨"));
                unavailable->setEnabled(false);
            }

            QMenu* subtitle_menu = menu.addMenu(QStringLiteral("字幕"));
            QAction* subtitles_enabled = subtitle_menu->addAction(QStringLiteral("启用字幕"));
            subtitles_enabled->setCheckable(true);
            subtitles_enabled->setChecked(subtitles_enabled_);
            QMenu* embedded_subtitle_menu = subtitle_menu->addMenu(QStringLiteral("内嵌字幕"));
            QAction* disable_embedded_subtitle =
                embedded_subtitle_menu->addAction(QStringLiteral("禁用内嵌字幕"));
            disable_embedded_subtitle->setCheckable(true);
            struct SubtitleTrackMenuEntry
            {
                int stream_index;
                QString label;
                bool is_selected;
            };
            std::vector<SubtitleTrackMenuEntry> embedded_subtitle_tracks;
            if (player_)
            {
                lnplayer_enumerate_subtitle_tracks(
                    player_,
                    [](const lnplayer_subtitle_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<SubtitleTrackMenuEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Subtitle");
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        entries->push_back({track->stream_index, label, track->is_selected != 0});
                    },
                    &embedded_subtitle_tracks);
            }
            bool has_selected_embedded_subtitle = false;
            for (const auto& track : embedded_subtitle_tracks)
            {
                QAction* action = embedded_subtitle_menu->addAction(track.label);
                action->setCheckable(true);
                action->setChecked(track.is_selected);
                action->setData(track.stream_index);
                has_selected_embedded_subtitle =
                    has_selected_embedded_subtitle || track.is_selected;
            }
            disable_embedded_subtitle->setChecked(!has_selected_embedded_subtitle);
            if (embedded_subtitle_tracks.empty())
            {
                QAction* unavailable =
                    embedded_subtitle_menu->addAction(QStringLiteral("无可用内嵌字幕"));
                unavailable->setEnabled(false);
            }
            QAction* subtitle_delay = subtitle_menu->addAction(QStringLiteral("字幕延迟 +500 ms"));
            QAction* subtitle_advance = subtitle_menu->addAction(QStringLiteral("字幕提前 500 ms"));
            QAction* subtitle_reset = subtitle_menu->addAction(QStringLiteral("重置字幕时间"));
            QAction* previous_frame = menu.addAction(QStringLiteral("上一帧"));
            QAction* next_frame = menu.addAction(QStringLiteral("下一帧"));
            QMenu* speed_menu = menu.addMenu(QStringLiteral("播放速度"));
            const float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
            for (float speed : speeds)
            {
                QAction* speed_action = speed_menu->addAction(
                    QStringLiteral("%1x").arg(speed, 0, 'f', 2));
                speed_action->setCheckable(true);
                speed_action->setChecked(std::abs(playback_rate_ - speed) < 0.001f);
                speed_action->setData(speed);
            }
            menu.addSeparator();
            QMenu* aspect_menu = menu.addMenu(QStringLiteral("画面比例"));
            QAction* fit = aspect_menu->addAction(QStringLiteral("原始比例"));
            QAction* ratio_16_9 = aspect_menu->addAction(QStringLiteral("16:9"));
            QAction* ratio_4_3 = aspect_menu->addAction(QStringLiteral("4:3"));
            QAction* stretch = aspect_menu->addAction(QStringLiteral("拉伸填充"));

            QMenu* filter_menu = menu.addMenu(QStringLiteral("视频滤镜"));
            QAction* brightness_down = filter_menu->addAction(QStringLiteral("亮度 -"));
            QAction* brightness_up = filter_menu->addAction(QStringLiteral("亮度 +"));
            QAction* contrast_down = filter_menu->addAction(QStringLiteral("对比度 -"));
            QAction* contrast_up = filter_menu->addAction(QStringLiteral("对比度 +"));
            QAction* saturation_down = filter_menu->addAction(QStringLiteral("饱和度 -"));
            QAction* saturation_up = filter_menu->addAction(QStringLiteral("饱和度 +"));
            QAction* hue_down = filter_menu->addAction(QStringLiteral("色调 -"));
            QAction* hue_up = filter_menu->addAction(QStringLiteral("色调 +"));
            QAction* sharpen_down = filter_menu->addAction(QStringLiteral("锐化 -"));
            QAction* sharpen_up = filter_menu->addAction(QStringLiteral("锐化 +"));
            QAction* denoise_down = filter_menu->addAction(QStringLiteral("降噪 -"));
            QAction* denoise_up = filter_menu->addAction(QStringLiteral("降噪 +"));
            QAction* grayscale = filter_menu->addAction(QStringLiteral("灰度"));
            grayscale->setCheckable(true);
            grayscale->setChecked(video_grayscale_);
            QAction* filter_reset = filter_menu->addAction(QStringLiteral("重置滤镜"));

            QAction* selected = menu.exec(video_surface_->mapToGlobal(point));
            if (selected == play_pause)
                togglePlayback();
            else if (selected == stop)
                QMetaObject::invokeMethod(command_worker_, [this]
                {
                    if (player_)
                        lnplayer_stop(player_);
                }, Qt::QueuedConnection);
            else if (selected == fullscreen)
            {
                if (isFullScreen())
                    showNormal();
                else
                    showFullScreen();
            }
            else if (selected == screenshot)
                takeScreenshot();
            else if (selected && selected->parent() == audio_track_menu)
            {
                const int stream_index = selected->data().toInt();
                QMetaObject::invokeMethod(command_worker_, [this, stream_index]
                {
                    if (!player_)
                        return;

                    lnplayer_snapshot_t snapshot{};
                    snapshot.struct_size = sizeof(snapshot);
                    if (lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK)
                        return;

                    const bool resume_playback = snapshot.state == LNPLAYER_STATE_PLAYING;
                    if (resume_playback && lnplayer_pause(player_) != LNPLAYER_RESULT_OK)
                        return;

                    const lnplayer_result_t result =
                        lnplayer_select_audio_track(player_, stream_index);
                    const lnplayer_result_t resume_result = resume_playback
                        ? lnplayer_play(player_)
                        : LNPLAYER_RESULT_OK;

                    QMetaObject::invokeMethod(this, [this, result, resume_playback, resume_result]
                    {
                        is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
                        if (result == LNPLAYER_RESULT_OK)
                        {
                            video_frame_history_.clear();
                            video_frame_history_index_ = -1;
                            pending_video_frame_valid_ = false;
                            requestVideoFrame();
                            statusBar()->showMessage(
                                resume_result == LNPLAYER_RESULT_OK
                                    ? QStringLiteral("音轨已切换")
                                    : QStringLiteral("音轨已切换，但恢复播放失败"));
                        }
                        else
                        {
                            statusBar()->showMessage(QStringLiteral("音轨切换失败"));
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }
            else if (selected == disable_embedded_subtitle ||
                (selected && selected->parent() == embedded_subtitle_menu))
            {
                const int stream_index = selected == disable_embedded_subtitle
                    ? -1
                    : selected->data().toInt();
                QMetaObject::invokeMethod(command_worker_, [this, stream_index]
                {
                    if (!player_)
                        return;

                    lnplayer_snapshot_t snapshot{};
                    snapshot.struct_size = sizeof(snapshot);
                    if (lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK)
                        return;

                    const bool resume_playback = snapshot.state == LNPLAYER_STATE_PLAYING;
                    if (resume_playback && lnplayer_pause(player_) != LNPLAYER_RESULT_OK)
                        return;

                    const lnplayer_result_t result =
                        lnplayer_select_subtitle_track(player_, stream_index);
                    const lnplayer_result_t resume_result = resume_playback
                        ? lnplayer_play(player_)
                        : LNPLAYER_RESULT_OK;

                    QMetaObject::invokeMethod(this, [this, result, resume_playback, resume_result]
                    {
                        is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
                        if (result == LNPLAYER_RESULT_OK)
                        {
                            pending_embedded_subtitle_.clear();
                            pending_embedded_subtitle_bitmap_ = {};
                            video_frame_history_.clear();
                            video_frame_history_index_ = -1;
                            pending_video_frame_valid_ = false;
                            requestVideoFrame();
                            statusBar()->showMessage(
                                resume_result == LNPLAYER_RESULT_OK
                                    ? QStringLiteral("内嵌字幕已切换")
                                    : QStringLiteral("内嵌字幕已切换，但恢复播放失败"));
                        }
                        else
                        {
                            statusBar()->showMessage(QStringLiteral("内嵌字幕切换失败"));
                        }
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }
            else if (selected == subtitles_enabled)
            {
                subtitles_enabled_ = subtitles_enabled->isChecked();
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }
            else if (selected == subtitle_delay || selected == subtitle_advance || selected == subtitle_reset)
            {
                if (selected == subtitle_delay)
                    subtitle_offset_ms_ += 500;
                else if (selected == subtitle_advance)
                    subtitle_offset_ms_ -= 500;
                else
                    subtitle_offset_ms_ = 0;
                const int64_t subtitle_offset_ms = subtitle_offset_ms_;
                QMetaObject::invokeMethod(command_worker_, [this, subtitle_offset_ms]
                {
                    if (player_)
                        lnplayer_set_subtitle_offset(player_, subtitle_offset_ms);
                    QMetaObject::invokeMethod(this, [this]
                    {
                        pending_video_frame_valid_ = false;
                        requestVideoFrame();
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
                statusBar()->showMessage(QStringLiteral("字幕时间偏移：%1 ms").arg(subtitle_offset_ms_));
            }
            else if (selected == previous_frame)
                stepVideoFrame(true);
            else if (selected == next_frame)
                stepVideoFrame();
            else if (selected && selected->parent() == speed_menu)
                setPlaybackRate(selected->data().toFloat());
            else if (selected == fit)
            {
                video_surface_->set_aspect_ratio(0.0f);
                requestVideoFrame();
            }
            else if (selected == ratio_16_9)
            {
                video_surface_->set_aspect_ratio(16.0f / 9.0f);
                requestVideoFrame();
            }
            else if (selected == ratio_4_3)
            {
                video_surface_->set_aspect_ratio(4.0f / 3.0f);
                requestVideoFrame();
            }
            else if (selected == stretch)
            {
                video_surface_->set_stretch(true);
                requestVideoFrame();
            }
            else if (selected == brightness_down || selected == brightness_up ||
                selected == contrast_down || selected == contrast_up ||
                selected == saturation_down || selected == saturation_up ||
                selected == hue_down || selected == hue_up ||
                selected == sharpen_down || selected == sharpen_up ||
                selected == denoise_down || selected == denoise_up ||
                selected == grayscale || selected == filter_reset)
            {
                if (selected == brightness_down)
                    video_brightness_ = std::clamp(video_brightness_ - 0.1f, -1.0f, 1.0f);
                else if (selected == brightness_up)
                    video_brightness_ = std::clamp(video_brightness_ + 0.1f, -1.0f, 1.0f);
                else if (selected == contrast_down)
                    video_contrast_ = std::clamp(video_contrast_ - 0.1f, 0.0f, 2.0f);
                else if (selected == contrast_up)
                    video_contrast_ = std::clamp(video_contrast_ + 0.1f, 0.0f, 2.0f);
                else if (selected == saturation_down)
                    video_saturation_ = std::clamp(video_saturation_ - 0.1f, 0.0f, 2.0f);
                else if (selected == saturation_up)
                    video_saturation_ = std::clamp(video_saturation_ + 0.1f, 0.0f, 2.0f);
                else if (selected == hue_down)
                    video_hue_degrees_ = std::clamp(video_hue_degrees_ - 5.0f, -180.0f, 180.0f);
                else if (selected == hue_up)
                    video_hue_degrees_ = std::clamp(video_hue_degrees_ + 5.0f, -180.0f, 180.0f);
                else if (selected == sharpen_down)
                    video_sharpness_ = std::clamp(video_sharpness_ - 0.1f, 0.0f, 2.0f);
                else if (selected == sharpen_up)
                    video_sharpness_ = std::clamp(video_sharpness_ + 0.1f, 0.0f, 2.0f);
                else if (selected == denoise_down)
                    video_denoise_ = std::clamp(video_denoise_ - 0.1f, 0.0f, 1.0f);
                else if (selected == denoise_up)
                    video_denoise_ = std::clamp(video_denoise_ + 0.1f, 0.0f, 1.0f);
                else if (selected == grayscale)
                    video_grayscale_ = grayscale->isChecked();
                else
                {
                    video_brightness_ = 0.0f;
                    video_contrast_ = 1.0f;
                    video_saturation_ = 1.0f;
                    video_hue_degrees_ = 0.0f;
                    video_sharpness_ = 0.0f;
                    video_denoise_ = 0.0f;
                    video_grayscale_ = false;
                }

                video_surface_->set_video_filter(
                    video_brightness_, video_contrast_, video_saturation_,
                    video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                statusBar()->showMessage(QStringLiteral("视频滤镜已更新"));
            }
        });
        auto* video_layout = new QHBoxLayout();
        video_layout->addWidget(video_surface_, 1);
        video_layout->addWidget(playlist_panel);
        layout->addLayout(video_layout, 1);
    }
    else
    {
        auto* audio_view = new QWidget(content);
        auto* audio_layout = new QVBoxLayout(audio_view);
        audio_layout->addStretch(1);
        media_title_label_->setAlignment(Qt::AlignCenter);
        QFont title_font = media_title_label_->font();
        title_font.setPointSize(16);
        media_title_label_->setFont(title_font);
        audio_layout->addWidget(media_title_label_);
        audio_layout->addStretch(1);
        library_search_->setPlaceholderText(QStringLiteral("搜索标题、艺术家或专辑"));
        auto* search_button = new QPushButton(QStringLiteral("搜索"), audio_view);
        auto* search_layout = new QHBoxLayout();
        search_layout->addWidget(library_search_, 1);
        search_layout->addWidget(search_button);
        audio_layout->addLayout(search_layout);
        audio_layout->addWidget(library_list_, 2);
        connect(search_button, &QPushButton::clicked, this, [this]
        {
            refreshLibrary();
        });
        connect(library_search_, &QLineEdit::returnPressed, this, [this]
        {
            refreshLibrary();
        });
        connect(library_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
        {
            const QString path = item->data(Qt::UserRole).toString();
            openFiles({QUrl::fromLocalFile(path)});
        });
        layout->addWidget(audio_view, 1);
    }

    seek_slider_->setRange(0, 1000);
    seek_slider_->setEnabled(false);
    volume_slider_->setRange(0, 1000);
    volume_slider_->setValue(1000);
    volume_slider_->setMaximumWidth(160);

    auto* seek_layout = new QHBoxLayout();
    seek_layout->addWidget(position_label_);
    seek_layout->addWidget(seek_slider_, 1);
    layout->addLayout(seek_layout);

    auto* control_layout = new QHBoxLayout();
    control_layout->addWidget(play_button_);
    control_layout->addWidget(pause_button_);
    control_layout->addWidget(stop_button_);
    auto* previous_button = new QPushButton(QStringLiteral("上一项"), content);
    auto* next_button = new QPushButton(QStringLiteral("下一项"), content);
    control_layout->addWidget(previous_button);
    control_layout->addWidget(next_button);
    connect(previous_button, &QPushButton::clicked, this, [this] { playPrevious(); });
    connect(next_button, &QPushButton::clicked, this, [this] { playNext(); });
    control_layout->addStretch(1);
    control_layout->addWidget(new QLabel(QStringLiteral("音量"), content));
    control_layout->addWidget(volume_slider_);
    layout->addLayout(control_layout);

    layout->addWidget(status_label_);
    setCentralWidget(content);

    if (mode_ == PlayerMode::Video)
    {
        auto bind_shortcut = [this](int key, std::function<void()> action)
        {
            auto* shortcut = new QShortcut(QKeySequence(key), this);
            shortcut->setContext(Qt::WindowShortcut);
            connect(shortcut, &QShortcut::activated, this, std::move(action));
        };
        bind_shortcut(Qt::Key_Space, [this] { togglePlayback(); });
        bind_shortcut(Qt::Key_F11, [this]
        {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
        });
        bind_shortcut(Qt::Key_Escape, [this]
        {
            if (isFullScreen())
                showNormal();
        });
        bind_shortcut(Qt::Key_Left, [this] { seekRelative(-5000); });
        bind_shortcut(Qt::Key_Right, [this] { seekRelative(5000); });
        bind_shortcut(Qt::Key_Comma, [this] { stepVideoFrame(true); });
        bind_shortcut(Qt::Key_Period, [this] { stepVideoFrame(); });
        bind_shortcut(Qt::Key_Up, [this]
        {
            volume_slider_->setValue(std::min(1000, volume_slider_->value() + 50));
        });
        bind_shortcut(Qt::Key_Down, [this]
        {
            volume_slider_->setValue(std::max(0, volume_slider_->value() - 50));
        });
        bind_shortcut(Qt::Key_M, [this]
        {
            if (volume_slider_->value() == 0)
                volume_slider_->setValue(muted_volume_);
            else
            {
                muted_volume_ = volume_slider_->value();
                volume_slider_->setValue(0);
            }
        });
    }
    statusBar()->showMessage(QStringLiteral("正在初始化播放器"));

    command_worker_->moveToThread(command_thread_);
    command_thread_->start();
    library_worker_->moveToThread(library_thread_);
    library_thread_->start();

    QMetaObject::invokeMethod(command_worker_, [this]
    {
        lnplayer_config_t config{};
        config.struct_size = sizeof(config);
        config.event_callback = +[](
            lnplayer_t*,
            const lnplayer_event_t* event,
            void* user_data)
        {
            auto* window = static_cast<MainWindow*>(user_data);
            const lnplayer_event_type_t type = event->type;
            const lnplayer_result_t result = event->result;
            const lnplayer_state_t state = event->state;
            const int64_t position_ms = event->position_ms;
            const int64_t duration_ms = event->duration_ms;
            const uint64_t generation = event->generation;
            const QString message = event->message
                ? QString::fromUtf8(event->message)
                : QString();

            QMetaObject::invokeMethod(window, [=]
            {
                window->handleCoreEvent(
                    type,
                    result,
                    state,
                    position_ms,
                    duration_ms,
                    generation,
                    message);
            }, Qt::QueuedConnection);
        };
        config.user_data = this;

        const lnplayer_result_t result = lnplayer_create(&config, &player_);
        if (result == LNPLAYER_RESULT_OK &&
            mode_ == PlayerMode::Video && video_surface_)
        {
            void* d3d11_device = video_surface_->native_device();
            if (d3d11_device)
            {
                const lnplayer_result_t device_result =
                    lnplayer_windows_set_d3d11_device(player_, d3d11_device);
                if (device_result == LNPLAYER_RESULT_OK)
                    video_device_identity_ = d3d11_device;
                else
                    static_cast<ID3D11Device*>(d3d11_device)->Release();
            }
        }
        QMetaObject::invokeMethod(this, [this, result]
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                status_label_->setText(QStringLiteral("播放器已就绪"));
                statusBar()->showMessage(QStringLiteral("就绪"));
                if (mode_ == PlayerMode::Audio)
                    refreshLibrary();
            }
            else
            {
                status_label_->setText(QStringLiteral("播放器初始化失败"));
                statusBar()->showMessage(QStringLiteral("Core 初始化失败"));
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);

    connect(play_button_, &QPushButton::clicked, this, [this]
    {
        if (video_seek_pending_)
            return;
        QMetaObject::invokeMethod(command_worker_, [this]
        {
            if (player_)
                lnplayer_play(player_);
        }, Qt::QueuedConnection);
    });

    connect(pause_button_, &QPushButton::clicked, this, [this]
    {
        QMetaObject::invokeMethod(command_worker_, [this]
        {
            if (player_)
                lnplayer_pause(player_);
        }, Qt::QueuedConnection);
    });

    connect(stop_button_, &QPushButton::clicked, this, [this]
    {
        QMetaObject::invokeMethod(command_worker_, [this]
        {
            if (player_)
                lnplayer_stop(player_);
        }, Qt::QueuedConnection);
    });

    connect(volume_slider_, &QSlider::valueChanged, this, [this](int value)
    {
        QMetaObject::invokeMethod(command_worker_, [this, value]
        {
            if (player_)
                lnplayer_set_volume(player_, static_cast<uint32_t>(value));
        }, Qt::QueuedConnection);
    });

    connect(seek_slider_, &QSlider::sliderPressed, this, [this]
    {
        slider_dragging_ = true;
    });
    connect(seek_slider_, &QSlider::sliderReleased, this, [this]
    {
        slider_dragging_ = false;
        viewing_historical_frame_ = false;
        video_frame_history_.clear();
        video_frame_history_index_ = -1;
        pending_video_frame_valid_ = false;
        video_end_of_stream_ = false;
        if (duration_ms_ <= 0)
        {
            video_seek_pending_ = false;
            return;
        }

        video_seek_pending_ = true;
        video_timer_->start();
        const int64_t target = duration_ms_ * seek_slider_->value() / 1000;
        QMetaObject::invokeMethod(command_worker_, [this, target]
        {
            uint64_t operation_id = 0;
            const lnplayer_result_t result = player_
                ? lnplayer_seek(player_, target, &operation_id)
                : LNPLAYER_RESULT_INVALID_HANDLE;
            if (result != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    requestVideoFrame();
                }, Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);
    });

    snapshot_timer_->setInterval(250);
    connect(snapshot_timer_, &QTimer::timeout, this, &MainWindow::refreshSnapshot);

    video_timer_->setInterval(16);
    connect(video_timer_, &QTimer::timeout, this, [this]
    {
        presentPendingVideoFrame();

        if (video_surface_)
        {
            void* current_device = video_surface_->native_device();
            if (current_device != video_device_identity_)
            {
                video_device_identity_ = current_device;
                QMetaObject::invokeMethod(command_worker_, [this, current_device]
                {
                    if (player_)
                    {
                        const lnplayer_result_t result =
                            lnplayer_windows_set_d3d11_device(player_, current_device);
                        if (result != LNPLAYER_RESULT_OK && current_device)
                            static_cast<ID3D11Device*>(current_device)->Release();
                    }
                    else if (current_device)
                    {
                        static_cast<ID3D11Device*>(current_device)->Release();
                    }
                }, Qt::QueuedConnection);
            }
            else if (current_device)
            {
                static_cast<ID3D11Device*>(current_device)->Release();
            }
        }

        if (is_playing_ && has_video_ && !video_frame_pending_ && !pending_video_frame_valid_)
            requestVideoFrame();
    });

    if (!initial_media_files.isEmpty())
    {
        QList<QUrl> initial_urls;
        initial_urls.reserve(initial_media_files.size());
        for (const QString& path : initial_media_files)
            initial_urls.append(QUrl::fromLocalFile(path));
        openFiles(initial_urls);
    }
    else if (mode_ == PlayerMode::Video)
        loadVideoPlaylist();
}

MainWindow::~MainWindow()
{
    saveVideoPlaylist();
    snapshot_timer_->stop();
    video_timer_->stop();

    if (library_thread_->isRunning())
    {
        QMetaObject::invokeMethod(library_worker_, [] {}, Qt::BlockingQueuedConnection);
        library_thread_->quit();
        library_thread_->wait();
    }
    delete library_worker_;

    if (command_thread_->isRunning())
    {
        QMetaObject::invokeMethod(command_worker_, [this]
        {
            if (player_)
            {
                lnplayer_destroy(player_);
                player_ = nullptr;
            }
        }, Qt::BlockingQueuedConnection);

        command_thread_->quit();
        command_thread_->wait();
    }

    delete command_worker_;
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

void MainWindow::openFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("打开媒体文件"),
        QString(),
        QStringLiteral("媒体文件 (*.mp3 *.flac *.wav *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.mp4 *.mkv *.mov *.avi *.webm);;所有文件 (*)"));

    QList<QUrl> urls;
    for (const QString& file : files)
        urls.append(QUrl::fromLocalFile(file));

    openFiles(urls);
}

void MainWindow::openFiles(const QList<QUrl>& urls)
{
    QList<QUrl> next_playlist;
    for (const QUrl& url : urls)
    {
        if (!url.toLocalFile().isEmpty())
            next_playlist.append(url);
    }
    if (next_playlist.isEmpty())
        return;

    playlist_ = std::move(next_playlist);
    playlist_index_ = 0;
    resetShuffleOrder();
    rebuildPlaylistWidget();
    saveVideoPlaylist();
    openPlaylistIndex(0);
}

void MainWindow::appendPlaylistFiles(const QList<QUrl>& urls)
{
    for (const QUrl& url : urls)
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        playlist_.append(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    }
    resetShuffleOrder();
    rebuildPlaylistWidget();
    saveVideoPlaylist();
    if (playlist_index_ < 0 && !playlist_.isEmpty())
    {
        playlist_index_ = 0;
        resetShuffleOrder();
        openPlaylistIndex(0);
    }
}

void MainWindow::rebuildPlaylistWidget()
{
    if (!playlist_widget_)
        return;
    playlist_widget_->clear();
    for (const QUrl& url : playlist_)
    {
        auto* item = new QListWidgetItem(QFileInfo(url.toLocalFile()).fileName(), playlist_widget_);
        item->setData(Qt::UserRole, url.toLocalFile());
    }
    playing_playlist_item_ = nullptr;
    if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
    {
        playlist_widget_->setCurrentRow(playlist_index_);
        playing_playlist_item_ = playlist_widget_->item(playlist_index_);
    }
}

void MainWindow::loadVideoPlaylist()
{
    QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
    const QStringList files = settings.value(QStringLiteral("VideoPlaylist/Files")).toStringList();
    playlist_.clear();
    for (const QString& path : files)
    {
        if (QFileInfo::exists(path))
            playlist_.append(QUrl::fromLocalFile(path));
    }

    playlist_index_ = playlist_.isEmpty()
        ? -1
        : std::clamp(
            settings.value(QStringLiteral("VideoPlaylist/CurrentIndex"), 0).toInt(),
            0, static_cast<int>(playlist_.size()) - 1);
    shuffle_enabled_ = settings.value(QStringLiteral("VideoPlaylist/Shuffle"), false).toBool();
    repeat_mode_ = std::clamp(
        settings.value(QStringLiteral("VideoPlaylist/RepeatMode"), 0).toInt(), 0, 2);
    if (shuffle_button_)
    {
        shuffle_button_->setChecked(shuffle_enabled_);
        shuffle_button_->setText(shuffle_enabled_
            ? QStringLiteral("随机：开")
            : QStringLiteral("随机：关"));
    }
    if (repeat_button_)
        repeat_button_->setText(repeat_mode_ == 0 ? QStringLiteral("循环：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环：列表") : QStringLiteral("循环：单项"));
    resetShuffleOrder();
    rebuildPlaylistWidget();
    if (playlist_index_ >= 0)
        openPlaylistIndex(playlist_index_);
}

void MainWindow::saveVideoPlaylist() const
{
    if (mode_ != PlayerMode::Video)
        return;

    QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
    QStringList files;
    for (const QUrl& url : playlist_)
        files.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
    settings.setValue(QStringLiteral("VideoPlaylist/Files"), files);
    settings.setValue(QStringLiteral("VideoPlaylist/CurrentIndex"), playlist_index_);
    settings.setValue(QStringLiteral("VideoPlaylist/Shuffle"), shuffle_enabled_);
    settings.setValue(QStringLiteral("VideoPlaylist/RepeatMode"), repeat_mode_);
    settings.sync();
}

void MainWindow::removeSelectedPlaylistItem()
{
    if (!playlist_widget_)
        return;
    const int row = playlist_widget_->currentRow();
    if (row < 0 || row >= playlist_.size())
        return;

    const bool removing_current = row == playlist_index_;
    playlist_.removeAt(row);
    if (row < playlist_index_)
        --playlist_index_;
    resetShuffleOrder();
    rebuildPlaylistWidget();

    if (removing_current)
    {
        if (playlist_.isEmpty())
        {
            playlist_index_ = -1;
            playlist_open_pending_ = false;
            video_timer_->stop();
            QMetaObject::invokeMethod(command_worker_, [this]
            {
                if (player_)
                    lnplayer_close(player_);
            }, Qt::QueuedConnection);
            if (video_surface_)
                video_surface_->set_frame(QImage());
        }
        else
        {
            openPlaylistIndex(std::min(row, static_cast<int>(playlist_.size()) - 1));
        }
    }
    saveVideoPlaylist();
}

void MainWindow::moveSelectedPlaylistItem(int offset)
{
    if (!playlist_widget_)
        return;
    const int from = playlist_widget_->currentRow();
    const int to = from + offset;
    if (from < 0 || to < 0 || from >= playlist_.size() || to >= playlist_.size())
        return;

    playlist_.swapItemsAt(from, to);
    if (playlist_index_ == from)
        playlist_index_ = to;
    else if (playlist_index_ == to)
        playlist_index_ = from;
    resetShuffleOrder();
    rebuildPlaylistWidget();
    playlist_widget_->setCurrentRow(to);
    saveVideoPlaylist();
}

void MainWindow::cycleRepeatMode()
{
    repeat_mode_ = (repeat_mode_ + 1) % 3;
    if (repeat_button_)
        repeat_button_->setText(repeat_mode_ == 0 ? QStringLiteral("循环：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环：列表") : QStringLiteral("循环：单项"));
    saveVideoPlaylist();
}

void MainWindow::resetShuffleOrder()
{
    shuffle_order_.clear();
    if (!shuffle_enabled_)
    {
        saveVideoPlaylist();
        return;
    }

    for (int index = 0; index < playlist_.size(); ++index)
        shuffle_order_.append(index);

    for (int index = shuffle_order_.size() - 1; index > 0; --index)
    {
        const int other = static_cast<int>(QRandomGenerator::global()->bounded(
            static_cast<quint32>(index + 1)));
        shuffle_order_.swapItemsAt(index, other);
    }

    if (playlist_index_ >= 0 && shuffle_order_.size() > 1)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current > 0)
            shuffle_order_.swapItemsAt(0, current);
    }
    saveVideoPlaylist();
}

int MainWindow::nextPlaylistIndex(bool manual) const
{
    if (playlist_.isEmpty() || playlist_index_ < 0)
        return -1;

    if (shuffle_enabled_)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current < 0)
            return -1;
        if (current + 1 < shuffle_order_.size())
            return shuffle_order_[current + 1];
        return (manual || repeat_mode_ == 1) && !shuffle_order_.isEmpty()
            ? shuffle_order_.front()
            : -1;
    }

    const int next = playlist_index_ + 1;
    if (next < playlist_.size())
        return next;
    return manual || repeat_mode_ == 1 ? 0 : -1;
}

void MainWindow::openPlaylistIndex(int index)
{
    if (index < 0 || index >= playlist_.size())
        return;

    playlist_index_ = index;
    saveVideoPlaylist();
    if (playlist_widget_)
    {
        playlist_widget_->setCurrentRow(index);
        playing_playlist_item_ = playlist_widget_->item(index);
    }

    const QString source = playlist_[index].toLocalFile();
    if (source.isEmpty())
        return;

    subtitle_cues_.clear();
    viewing_historical_frame_ = false;
    video_seek_pending_ = false;
    pending_embedded_subtitle_.clear();
    pending_embedded_subtitle_bitmap_ = {};
    pending_video_display_aspect_ratio_ = 0.0f;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    pending_hardware_frame_owner_.reset();
    pending_hardware_texture_ = nullptr;
    video_first_frame_received_ = false;
    video_end_of_stream_ = false;
    video_timer_->stop();
    if (video_surface_)
        video_surface_->set_frame(QImage());

    media_title_label_->setText(QFileInfo(source).fileName());
    status_label_->setText(QStringLiteral("正在打开：%1").arg(source));
    statusBar()->showMessage(QStringLiteral("正在打开媒体"));
    playlist_open_pending_ = true;

    const QByteArray source_utf8 = source.toUtf8();
    QMetaObject::invokeMethod(command_worker_, [this, source_utf8]
    {
        if (!player_)
            return;

        uint64_t operation_id = 0;
        const lnplayer_result_t result = lnplayer_open(
            player_,
            source_utf8.constData(),
            &operation_id);

        if (result != LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this]
            {
                playlist_open_pending_ = false;
                status_label_->setText(QStringLiteral("媒体打开请求失败"));
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void MainWindow::playPrevious()
{
    if (playlist_.isEmpty() || playlist_index_ < 0)
        return;

    if (shuffle_enabled_)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current < 0 || shuffle_order_.isEmpty())
            return;
        const int previous = current > 0 ? current - 1 : shuffle_order_.size() - 1;
        openPlaylistIndex(shuffle_order_[previous]);
        return;
    }

    const int previous = playlist_index_ > 0
        ? playlist_index_ - 1
        : playlist_.size() - 1;
    openPlaylistIndex(previous);
}

void MainWindow::playNext()
{
    const int next = nextPlaylistIndex(true);
    if (next >= 0)
        openPlaylistIndex(next);
}

void MainWindow::loadExternalSubtitle()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载外挂字幕"),
        QString(),
        QStringLiteral("字幕文件 (*.srt *.ass *.ssa);;SubRip (*.srt);;ASS/SSA (*.ass *.ssa);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        status_label_->setText(QStringLiteral("无法打开字幕文件"));
        return;
    }

    QByteArray subtitle_data = file.readAll();
    QString subtitle_text;
    if (subtitle_data.startsWith("\\xEF\\xBB\\xBF"))
    {
        subtitle_text = QString::fromUtf8(subtitle_data.constData() + 3, subtitle_data.size() - 3);
    }
    else if (subtitle_data.startsWith("\\xFF\\xFE\\x00\\x00"))
    {
        QStringDecoder decoder("UTF-32LE");
        subtitle_text = decoder(subtitle_data.mid(4));
        if (decoder.hasError())
        {
            status_label_->setText(QStringLiteral("UTF-32LE 字幕编码无效"));
            return;
        }
    }
    else if (subtitle_data.startsWith("\\x00\\x00\\xFE\\xFF"))
    {
        QStringDecoder decoder("UTF-32BE");
        subtitle_text = decoder(subtitle_data.mid(4));
        if (decoder.hasError())
        {
            status_label_->setText(QStringLiteral("UTF-32BE 字幕编码无效"));
            return;
        }
    }
    else if (subtitle_data.startsWith("\\xFF\\xFE"))
    {
        QStringDecoder decoder("UTF-16LE");
        subtitle_text = decoder(subtitle_data.mid(2));
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }
    else if (subtitle_data.startsWith("\\xFE\\xFF"))
    {
        QStringDecoder decoder("UTF-16BE");
        subtitle_text = decoder(subtitle_data.mid(2));
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }
    else
    {
        QStringDecoder decoder(QStringDecoder::Utf8);
        subtitle_text = decoder(subtitle_data);
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }
    const QStringList lines = subtitle_text.split(
        QRegularExpression(QStringLiteral("\\r?\\n")));
    const QRegularExpression srt_timestamp(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})[,.](\\d{1,3})$"));
    const QRegularExpression ass_timestamp(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})\\.(\\d{1,2})$"));

    const auto parse_timestamp = [](const QRegularExpression& expression,
                                    const QString& value,
                                    int64_t& output,
                                    int fraction_digits)
    {
        const auto match = expression.match(value.trimmed());
        if (!match.hasMatch())
            return false;

        const int hours = match.captured(1).toInt();
        const int minutes = match.captured(2).toInt();
        const int seconds = match.captured(3).toInt();
        QString fraction = match.captured(4);
        while (fraction.size() < fraction_digits)
            fraction.append(QLatin1Char('0'));
        const int milliseconds = fraction.left(fraction_digits).toInt() *
            (fraction_digits == 2 ? 10 : 1);
        output = ((static_cast<int64_t>(hours) * 60 + minutes) * 60 + seconds) *
            1000 + milliseconds;
        return true;
    };

    std::vector<SubtitleCue> cues;
    const bool ass_format = path.endsWith(QStringLiteral(".ass"), Qt::CaseInsensitive) ||
        path.endsWith(QStringLiteral(".ssa"), Qt::CaseInsensitive);

    if (ass_format)
    {
        QStringList format_fields{
            QStringLiteral("Layer"), QStringLiteral("Start"), QStringLiteral("End"),
            QStringLiteral("Style"), QStringLiteral("Name"), QStringLiteral("MarginL"),
            QStringLiteral("MarginR"), QStringLiteral("MarginV"),
            QStringLiteral("Effect"), QStringLiteral("Text")
        };
        int start_index = format_fields.indexOf(QStringLiteral("Start"), Qt::CaseInsensitive);
        int end_index = format_fields.indexOf(QStringLiteral("End"), Qt::CaseInsensitive);
        int text_index = format_fields.indexOf(QStringLiteral("Text"), Qt::CaseInsensitive);

        for (const QString& line : lines)
        {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("Format:"), Qt::CaseInsensitive))
            {
                format_fields = trimmed.mid(7).split(QLatin1Char(','));
                for (QString& field : format_fields)
                    field = field.trimmed();
                start_index = format_fields.indexOf(QStringLiteral("Start"), Qt::CaseInsensitive);
                end_index = format_fields.indexOf(QStringLiteral("End"), Qt::CaseInsensitive);
                text_index = format_fields.indexOf(QStringLiteral("Text"), Qt::CaseInsensitive);
                continue;
            }
            if (!trimmed.startsWith(QStringLiteral("Dialogue:"), Qt::CaseInsensitive) ||
                start_index < 0 || end_index < 0 || text_index < 0)
                continue;

            const QString payload = trimmed.mid(9).trimmed();
            QStringList fields;
            int cursor = 0;
            for (int field = 0; field < text_index; ++field)
            {
                const int comma = payload.indexOf(QLatin1Char(','), cursor);
                if (comma < 0)
                    break;
                fields.append(payload.mid(cursor, comma - cursor).trimmed());
                cursor = comma + 1;
            }
            fields.append(payload.mid(cursor));
            if (fields.size() <= std::max({start_index, end_index, text_index}))
                continue;

            SubtitleCue cue;
            if (!parse_timestamp(ass_timestamp, fields[start_index], cue.start_ms, 2) ||
                !parse_timestamp(ass_timestamp, fields[end_index], cue.end_ms, 2) ||
                cue.end_ms <= cue.start_ms)
                continue;

            cue.text = fields[text_index];
            cue.text.remove(QRegularExpression(QStringLiteral("\\{[^}]*\\}")));
            cue.text.replace(QStringLiteral("\\N"), QStringLiteral("\n"));
            cue.text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            cue.text.replace(QStringLiteral("\\h"), QStringLiteral(" "));
            if (!cue.text.trimmed().isEmpty())
                cues.push_back(std::move(cue));
        }
    }
    else
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            if (!lines[i].contains(QStringLiteral("-->")))
                continue;

            const QStringList times = lines[i].split(QStringLiteral("-->"));
            if (times.size() != 2)
                continue;

            SubtitleCue cue;
            const QString end_timestamp = times[1].trimmed().section(QLatin1Char(' '), 0, 0);
            if (!parse_timestamp(srt_timestamp, times[0], cue.start_ms, 3) ||
                !parse_timestamp(srt_timestamp, end_timestamp, cue.end_ms, 3) ||
                cue.end_ms <= cue.start_ms)
                continue;

            QStringList text_lines;
            for (++i; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i)
                text_lines.append(lines[i].trimmed());

            cue.text = text_lines.join(QLatin1Char('\n'));
            cue.text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            cue.text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            cue.text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
            cue.text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
            if (!cue.text.isEmpty())
                cues.push_back(std::move(cue));
        }
    }

    std::sort(cues.begin(), cues.end(), [](const SubtitleCue& left, const SubtitleCue& right)
    {
        return left.start_ms < right.start_ms;
    });

    subtitle_cues_ = std::move(cues);
    status_label_->setText(QStringLiteral("已加载 %1 条字幕").arg(subtitle_cues_.size()));
    pending_video_frame_valid_ = false;
    requestVideoFrame();
}

QString MainWindow::subtitleAt(int64_t position_ms) const
{
    if (!subtitles_enabled_)
        return {};
    position_ms -= subtitle_offset_ms_;
    const auto next = std::upper_bound(
        subtitle_cues_.begin(),
        subtitle_cues_.end(),
        position_ms,
        [](int64_t position, const SubtitleCue& cue)
        {
            return position < cue.start_ms;
        });
    if (next == subtitle_cues_.begin())
        return {};

    const SubtitleCue& cue = *std::prev(next);
    return position_ms < cue.end_ms ? cue.text : QString();
}

void MainWindow::refreshLibrary()
{
    lnplayer_t* player_handle = nullptr;
    QMetaObject::invokeMethod(command_worker_, [this, &player_handle]
    {
        player_handle = player_;
    }, Qt::BlockingQueuedConnection);
    if (!player_handle)
        return;

    const QByteArray query_utf8 = library_search_->text().toUtf8();
    library_list_->clear();

    QMetaObject::invokeMethod(library_worker_, [this, player_handle, query_utf8]
    {
        struct SearchContext
        {
            MainWindow* window;
        } context{this};

        const lnplayer_result_t result = lnplayer_library_search_tracks(
            player_handle,
            query_utf8.constData(),
            500,
            +[](const lnplayer_library_track_t* track, void* user_data)
            {
                auto* context = static_cast<SearchContext*>(user_data);
                const QString title = QString::fromUtf8(track->title_utf8);
                const QString artist = QString::fromUtf8(track->artist_utf8);
                const QString path = QString::fromUtf8(track->path_utf8);
                const QString label = artist.isEmpty()
                    ? title
                    : QStringLiteral("%1 — %2").arg(title, artist);
                QMetaObject::invokeMethod(context->window, [window = context->window, label, path]
                {
                    auto* item = new QListWidgetItem(label, window->library_list_);
                    item->setData(Qt::UserRole, path);
                }, Qt::QueuedConnection);
            },
            &context);

        QMetaObject::invokeMethod(this, [this, result]
        {
            if (result != LNPLAYER_RESULT_OK)
                status_label_->setText(QStringLiteral("音乐库查询失败"));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::requestVideoFrame()
{
    if (mode_ != PlayerMode::Video || !has_video_ || video_frame_pending_ || video_end_of_stream_)
        return;

    video_frame_pending_ = true;
    QMetaObject::invokeMethod(command_worker_, [this]
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
        const int ycbcr_matrix = context.ycbcr_matrix;
        const int ycbcr_full_range = context.ycbcr_full_range;
        const QString embedded_subtitle = context.subtitle;
        const QImage embedded_subtitle_bitmap = context.subtitle_bitmap;
        QMetaObject::invokeMethod(this,
            [this, result, end_of_stream, image, hardware_owner, hardware_texture,
             hardware_array_slice, hardware_format, hardware_width, hardware_height,
             pts_ms, generation, requested_generation, display_aspect_ratio,
             hdr_transfer, color_primaries, ycbcr_matrix, ycbcr_full_range,
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

            if ((!image.isNull() || hardware_owner) &&
                generation == requested_generation && generation >= media_generation_)
            {
                media_generation_ = generation;
                video_seek_pending_ = false;
                video_end_of_stream_ = false;
                video_first_frame_received_ = true;
                if (video_surface_)
                {
                    video_surface_->set_hdr_metadata(hdr_transfer, color_primaries);
                    video_surface_->set_video_color_space(ycbcr_matrix, ycbcr_full_range);
                    video_surface_->set_source_aspect_ratio(display_aspect_ratio);
                }
                pending_video_frame_ = image;
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
                pending_video_frame_valid_ = true;
                presentPendingVideoFrame();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::presentPendingVideoFrame()
{
    if (!pending_video_frame_valid_ || !video_surface_)
        return;

    if (is_playing_)
    {
        const int64_t clock_ms = current_position_ms_;
        if (pending_video_pts_ms_ > clock_ms + 8)
            return;
    }

    viewing_historical_frame_ = false;
    if (pending_hardware_frame_owner_)
    {
        QString subtitle = subtitleAt(pending_video_pts_ms_);
        if (subtitles_enabled_ && !pending_embedded_subtitle_.isEmpty())
        {
            if (!subtitle.isEmpty())
                subtitle += QLatin1Char('\n');
            subtitle += pending_embedded_subtitle_;
        }
        QImage overlay(video_surface_->size(), QImage::Format_ARGB32);
        overlay.fill(Qt::transparent);
        if (subtitles_enabled_ && !pending_embedded_subtitle_bitmap_.isNull())
        {
            const float aspect = pending_video_display_aspect_ratio_ > 0.0f
                ? pending_video_display_aspect_ratio_
                : static_cast<float>(pending_embedded_subtitle_bitmap_.width()) /
                    pending_embedded_subtitle_bitmap_.height();
            int target_width = overlay.width();
            int target_height = static_cast<int>(target_width / aspect);
            if (target_height > overlay.height())
            {
                target_height = overlay.height();
                target_width = static_cast<int>(target_height * aspect);
            }
            const QRect target(
                (overlay.width() - target_width) / 2,
                (overlay.height() - target_height) / 2,
                target_width,
                target_height);
            QPainter bitmap_painter(&overlay);
            bitmap_painter.drawImage(target, pending_embedded_subtitle_bitmap_);
        }
        if (!subtitle.isEmpty())
        {
            QPainter painter(&overlay);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            QFont font(QStringLiteral("Segoe UI"));
            font.setPixelSize(std::max(20, overlay.width() / 40));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QRect text_rect(
                overlay.width() / 20,
                overlay.height() * 4 / 5,
                overlay.width() * 9 / 10,
                overlay.height() / 6);
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
            painter.setPen(Qt::white);
            painter.drawText(
                text_rect,
                Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                subtitle);
        }

        VideoFrameHistoryEntry history_entry;
        history_entry.hardware_owner = pending_hardware_frame_owner_;
        history_entry.hardware_texture = pending_hardware_texture_;
        history_entry.hardware_array_slice = pending_hardware_array_slice_;
        history_entry.hardware_format = pending_hardware_format_;
        history_entry.hardware_width = pending_hardware_width_;
        history_entry.hardware_height = pending_hardware_height_;
        history_entry.hdr_transfer = hdr_transfer;
        history_entry.color_primaries = color_primaries;
        history_entry.display_aspect_ratio = display_aspect_ratio;
        history_entry.ycbcr_matrix = ycbcr_matrix;
        history_entry.ycbcr_full_range = ycbcr_full_range;
        history_entry.embedded_subtitle = pending_embedded_subtitle_;
        history_entry.embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
        history_entry.pts_ms = pending_video_pts_ms_;
        history_entry.generation = pending_video_generation_;
        if (video_frame_history_index_ + 1 < video_frame_history_.size())
            video_frame_history_.erase(
                video_frame_history_.begin() + video_frame_history_index_ + 1,
                video_frame_history_.end());
        video_frame_history_.append(std::move(history_entry));
        constexpr int maximum_history_frames = 120;
        if (video_frame_history_.size() > maximum_history_frames)
            video_frame_history_.removeFirst();
        video_frame_history_index_ = video_frame_history_.size() - 1;

        auto owner = std::move(pending_hardware_frame_owner_);
        void* texture = pending_hardware_texture_;
        const uint32_t array_slice = pending_hardware_array_slice_;
        const uint32_t format = pending_hardware_format_;
        const int width = pending_hardware_width_;
        const int height = pending_hardware_height_;
        const uint64_t generation = pending_video_generation_;
        const int64_t pts_ms = pending_video_pts_ms_;
        pending_hardware_texture_ = nullptr;
        pending_video_frame_valid_ = false;

        QMetaObject::invokeMethod(command_worker_,
            [this, owner = std::move(owner), texture, array_slice, format,
             width, height, generation, pts_ms, subtitle, overlay = std::move(overlay)]
        {
            lnplayer_snapshot_t snapshot{};
            snapshot.struct_size = sizeof(snapshot);
            if (!player_ || lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK ||
                snapshot.generation != generation)
                return;

            const auto fallback_owner = owner;
            const bool presented = video_surface_ &&
                video_surface_->present_hardware_frame(
                    texture, array_slice, format, width, height,
                    std::move(owner), overlay);
            if (!presented)
            {
                struct FallbackFrame
                {
                    QImage image;
                    float display_aspect_ratio = 0.0f;
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
                            output->display_aspect_ratio = frame->display_aspect_ratio;
                        },
                        &fallback);

                if (fallback_result == LNPLAYER_RESULT_OK && !fallback.image.isNull())
                {
                    if (subtitles_enabled_ && !pending_embedded_subtitle_bitmap_.isNull())
                    {
                        QPainter bitmap_painter(&fallback.image);
                        bitmap_painter.drawImage(fallback.image.rect(), pending_embedded_subtitle_bitmap_);
                    }
                    if (!subtitle.isEmpty())
                    {
                        QPainter painter(&fallback.image);
                        painter.setRenderHint(QPainter::TextAntialiasing, true);
                        QFont font(QStringLiteral("Segoe UI"));
                        font.setPixelSize(std::max(20, fallback.image.width() / 40));
                        font.setWeight(QFont::DemiBold);
                        painter.setFont(font);
                        const QRect text_rect(
                            fallback.image.width() / 20,
                            fallback.image.height() * 4 / 5,
                            fallback.image.width() * 9 / 10,
                            fallback.image.height() / 6);
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
                        painter.setPen(Qt::white);
                        painter.drawText(
                            text_rect,
                            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                            subtitle);
                    }

                    if (video_surface_)
                        video_surface_->present_frame_threadsafe(fallback.image);
                    QMetaObject::invokeMethod(this, [this, image = fallback.image]
                    {
                        last_video_frame_ = image;
                    }, Qt::QueuedConnection);
                }
                else
                {
                    QMetaObject::invokeMethod(this, [this]
                    {
                        status_label_->setText(QStringLiteral("硬件视频帧渲染失败，软件回退也失败"));
                    }, Qt::QueuedConnection);
                }
            }
        }, Qt::QueuedConnection);
        return;
    }

    QImage frame = pending_video_frame_;
    VideoFrameHistoryEntry history_entry;
    history_entry.image = frame;
    history_entry.hdr_transfer = hdr_transfer;
    history_entry.color_primaries = color_primaries;
    history_entry.display_aspect_ratio = display_aspect_ratio;
    history_entry.ycbcr_matrix = ycbcr_matrix;
    history_entry.ycbcr_full_range = ycbcr_full_range;
    history_entry.embedded_subtitle = pending_embedded_subtitle_;
    history_entry.embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
    history_entry.pts_ms = pending_video_pts_ms_;
    history_entry.generation = pending_video_generation_;
    if (video_frame_history_index_ + 1 < video_frame_history_.size())
        video_frame_history_.erase(
            video_frame_history_.begin() + video_frame_history_index_ + 1,
            video_frame_history_.end());
    video_frame_history_.append(std::move(history_entry));
    constexpr int maximum_history_frames = 120;
    if (video_frame_history_.size() > maximum_history_frames)
        video_frame_history_.removeFirst();
    video_frame_history_index_ = video_frame_history_.size() - 1;
    if (subtitles_enabled_ && !pending_embedded_subtitle_bitmap_.isNull())
    {
        QPainter bitmap_painter(&frame);
        bitmap_painter.drawImage(frame.rect(), pending_embedded_subtitle_bitmap_);
    }
    QString subtitle = subtitleAt(pending_video_pts_ms_);
    if (subtitles_enabled_ && !pending_embedded_subtitle_.isEmpty())
    {
        if (!subtitle.isEmpty())
            subtitle += QLatin1Char('\n');
        subtitle += pending_embedded_subtitle_;
    }
    if (!subtitle.isEmpty())
    {
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(std::max(20, frame.width() / 40));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        const QRect text_rect(
            frame.width() / 20,
            frame.height() * 4 / 5,
            frame.width() * 9 / 10,
            frame.height() / 6);
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
        painter.setPen(Qt::white);
        painter.drawText(
            text_rect,
            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            subtitle);
    }
    last_video_frame_ = frame;
    if (video_surface_->has_renderer())
    {
        QMetaObject::invokeMethod(command_worker_, [this, frame]
        {
            if (video_surface_ && !video_surface_->present_frame_threadsafe(frame))
            {
                QMetaObject::invokeMethod(this, [this, frame]
                {
                    if (video_surface_ && !video_surface_->has_renderer())
                        video_surface_->set_frame(frame);
                }, Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);
    }
    else
    {
        video_surface_->set_frame(frame);
    }
    pending_video_frame_ = {};
    pending_video_frame_valid_ = false;
}
void MainWindow::togglePlayback()
{
    if (video_seek_pending_)
        return;
    const bool should_pause = is_playing_;
    const bool resume_from_history = !should_pause && viewing_historical_frame_;
    const int64_t resume_position = current_position_ms_;
    if (resume_from_history)
    {
        viewing_historical_frame_ = false;
        video_frame_history_.clear();
        video_frame_history_index_ = -1;
        pending_video_frame_valid_ = false;
        video_end_of_stream_ = false;
        video_seek_pending_ = true;
        video_timer_->start();
    }

    QMetaObject::invokeMethod(command_worker_,
        [this, should_pause, resume_from_history, resume_position]
    {
        if (!player_)
        {
            if (resume_from_history)
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                }, Qt::QueuedConnection);
            return;
        }
        if (should_pause)
        {
            lnplayer_pause(player_);
            return;
        }
        if (resume_from_history)
        {
            uint64_t operation_id = 0;
            if (lnplayer_seek(player_, resume_position, &operation_id) != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    requestVideoFrame();
                }, Qt::QueuedConnection);
                return;
            }
        }
        lnplayer_play(player_);
    }, Qt::QueuedConnection);
}

void MainWindow::seekRelative(int64_t offset_ms)
{
    if (duration_ms_ <= 0)
        return;

    viewing_historical_frame_ = false;
    video_seek_pending_ = true;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    video_end_of_stream_ = false;
    video_timer_->start();
    const int64_t target = std::clamp<int64_t>(
        current_position_ms_ + offset_ms, 0, duration_ms_);
    QMetaObject::invokeMethod(command_worker_, [this, target]
    {
        uint64_t operation_id = 0;
        const lnplayer_result_t result = player_
            ? lnplayer_seek(player_, target, &operation_id)
            : LNPLAYER_RESULT_INVALID_HANDLE;
        if (result != LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this]
            {
                video_seek_pending_ = false;
                requestVideoFrame();
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void MainWindow::takeScreenshot()
{
    QImage screenshot = video_surface_ ? video_surface_->capture_frame() : QImage();
    if (screenshot.isNull())
        screenshot = last_video_frame_;
    if (screenshot.isNull())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存视频截图"),
        QStringLiteral("screenshot.png"),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg)"));
    if (!path.isEmpty())
        screenshot.save(path);
}

void MainWindow::setPlaybackRate(float rate)
{
    playback_rate_ = rate;

    QMetaObject::invokeMethod(command_worker_, [this, rate]
    {
        if (player_)
            lnplayer_set_playback_rate(player_, rate);
    }, Qt::QueuedConnection);
}

void MainWindow::stepVideoFrame(bool backwards)
{
    if (mode_ != PlayerMode::Video || !has_video_)
        return;

    if (is_playing_)
    {
        QMetaObject::invokeMethod(command_worker_, [this]
        {
            if (player_)
                lnplayer_pause(player_);
        }, Qt::QueuedConnection);
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

void MainWindow::presentHistoricalVideoFrame()
{
    if (!video_surface_ ||
        video_frame_history_index_ < 0 ||
        video_frame_history_index_ >= video_frame_history_.size())
        return;

    const VideoFrameHistoryEntry entry = video_frame_history_[video_frame_history_index_];
    viewing_historical_frame_ = true;
    current_position_ms_ = entry.pts_ms;
    const QString duration_text = duration_ms_ < 0
        ? QStringLiteral("--:--")
        : format_position(duration_ms_);
    position_label_->setText(
        QStringLiteral("%1 / %2").arg(format_position(entry.pts_ms), duration_text));
    if (!slider_dragging_ && duration_ms_ > 0)
        seek_slider_->setValue(static_cast<int>(
            std::clamp<int64_t>(entry.pts_ms * 1000 / duration_ms_, 0, 1000)));
    if (video_surface_)
    {
        video_surface_->set_hdr_metadata(entry.hdr_transfer, entry.color_primaries);
        video_surface_->set_video_color_space(entry.ycbcr_matrix, entry.ycbcr_full_range);
        video_surface_->set_source_aspect_ratio(entry.display_aspect_ratio);
    }
    QImage frame = entry.image;
    if (subtitles_enabled_ && !entry.embedded_subtitle_bitmap.isNull() && !entry.hardware_owner)
    {
        QPainter bitmap_painter(&frame);
        bitmap_painter.drawImage(frame.rect(), entry.embedded_subtitle_bitmap);
    }
    QString subtitle = subtitleAt(entry.pts_ms);
    if (subtitles_enabled_ && !entry.embedded_subtitle.isEmpty())
    {
        if (!subtitle.isEmpty())
            subtitle += QLatin1Char('\n');
        subtitle += entry.embedded_subtitle;
    }
    if (entry.hardware_owner)
    {
        QImage overlay(video_surface_->size(), QImage::Format_ARGB32);
        overlay.fill(Qt::transparent);
        if (subtitles_enabled_ && !entry.embedded_subtitle_bitmap.isNull())
        {
            const float aspect = entry.display_aspect_ratio > 0.0f
                ? entry.display_aspect_ratio
                : static_cast<float>(entry.embedded_subtitle_bitmap.width()) /
                    entry.embedded_subtitle_bitmap.height();
            int target_width = overlay.width();
            int target_height = static_cast<int>(target_width / aspect);
            if (target_height > overlay.height())
            {
                target_height = overlay.height();
                target_width = static_cast<int>(target_height * aspect);
            }
            const QRect target(
                (overlay.width() - target_width) / 2,
                (overlay.height() - target_height) / 2,
                target_width,
                target_height);
            QPainter bitmap_painter(&overlay);
            bitmap_painter.drawImage(target, entry.embedded_subtitle_bitmap);
        }
        if (!subtitle.isEmpty())
        {
            QPainter painter(&overlay);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            QFont font(QStringLiteral("Segoe UI"));
            font.setPixelSize(std::max(20, overlay.width() / 40));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QRect text_rect(
                overlay.width() / 20,
                overlay.height() * 4 / 5,
                overlay.width() * 9 / 10,
                overlay.height() / 6);
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
            painter.setPen(Qt::white);
            painter.drawText(
                text_rect,
                Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                subtitle);
        }

        QMetaObject::invokeMethod(command_worker_,
            [this, entry, overlay = std::move(overlay)]() mutable
        {
            lnplayer_snapshot_t snapshot{};
            snapshot.struct_size = sizeof(snapshot);
            if (!player_ ||
                lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK ||
                snapshot.generation != entry.generation)
                return;

            if (!video_surface_->present_hardware_frame(
                    entry.hardware_texture,
                    entry.hardware_array_slice,
                    entry.hardware_format,
                    entry.hardware_width,
                    entry.hardware_height,
                    entry.hardware_owner,
                    overlay))
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    status_label_->setText(QStringLiteral("无法重新呈现历史硬件视频帧"));
                }, Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);
        return;
    }

    if (!subtitle.isEmpty())
    {
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(std::max(20, frame.width() / 40));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        const QRect text_rect(
            frame.width() / 20,
            frame.height() * 4 / 5,
            frame.width() * 9 / 10,
            frame.height() / 6);
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
        painter.setPen(Qt::white);
        painter.drawText(
            text_rect,
            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            subtitle);
    }

    last_video_frame_ = frame;
    video_surface_->set_frame(frame);
}

void MainWindow::handleCoreEvent(
    lnplayer_event_type_t type,
    lnplayer_result_t result,
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms,
    uint64_t generation,
    const QString& message)
{
    if (generation < media_generation_)
        return;
    media_generation_ = generation;
    updatePlaybackState(state, position_ms, duration_ms);

    switch (type)
    {
    case LNPLAYER_EVENT_OPEN_COMPLETED:
        playlist_open_pending_ = false;
        if (result == LNPLAYER_RESULT_OK)
        {
            status_label_->setText(QStringLiteral("媒体已打开"));
            statusBar()->showMessage(QStringLiteral("就绪"));
            seek_slider_->setEnabled(duration_ms > 0);
            snapshot_timer_->setInterval(mode_ == PlayerMode::Video ? 25 : 250);
            snapshot_timer_->start();
            refreshSnapshot();
            pending_video_frame_valid_ = false;
            pending_hardware_frame_owner_.reset();
            pending_hardware_texture_ = nullptr;
            video_first_frame_received_ = false;
            video_end_of_stream_ = false;
            last_video_frame_ = {};
            if (video_surface_)
                video_surface_->set_frame(QImage());
        }
        else
        {
            status_label_->setText(message.isEmpty()
                ? QStringLiteral("媒体打开失败")
                : message);
            statusBar()->showMessage(QStringLiteral("打开失败"));
        }
        break;

    case LNPLAYER_EVENT_SEEK_COMPLETED:
        if (mode_ == PlayerMode::Video)
        {
            video_seek_pending_ = false;
            pending_video_frame_valid_ = false;
            pending_hardware_frame_owner_.reset();
            pending_hardware_texture_ = nullptr;
            video_first_frame_received_ = false;
            video_end_of_stream_ = false;
            requestVideoFrame();
        }
        break;

    case LNPLAYER_EVENT_PLAYBACK_ENDED:
        if (playlist_open_pending_ || video_seek_pending_)
            break;
        if (repeat_mode_ == 2 && playlist_index_ >= 0)
        {
            openPlaylistIndex(playlist_index_);
            break;
        }
        const int next_index = nextPlaylistIndex(false);
        if (next_index >= 0)
        {
            openPlaylistIndex(next_index);
            break;
        }
        video_timer_->stop();
        status_label_->setText(QStringLiteral("播放结束"));
        break;

    case LNPLAYER_EVENT_ERROR:
        status_label_->setText(message.isEmpty()
            ? QStringLiteral("播放发生错误")
            : message);
        break;

    case LNPLAYER_EVENT_STATE_CHANGED:
    default:
        break;
    }
}

void MainWindow::refreshSnapshot()
{
    QMetaObject::invokeMethod(command_worker_, [this]
    {
        if (!player_)
            return;

        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK)
            return;

        const lnplayer_state_t state = snapshot.state;
        const int64_t position_ms = snapshot.position_ms;
        const int64_t duration_ms = snapshot.duration_ms;
        const uint64_t generation = snapshot.generation;
        const bool has_audio = (snapshot.reserved & LNPLAYER_MEDIA_HAS_AUDIO) != 0;
        const bool has_video = (snapshot.reserved & LNPLAYER_MEDIA_HAS_VIDEO) != 0;

        QMetaObject::invokeMethod(this, [this, state, position_ms, duration_ms, generation, has_audio, has_video]
        {
            media_generation_ = std::max(media_generation_, generation);
            has_audio_ = has_audio;
            has_video_ = has_video;
            updatePlaybackState(state, position_ms, duration_ms);
            if (has_video_ && !video_first_frame_received_ &&
                !pending_video_frame_valid_ && !video_frame_pending_)
                requestVideoFrame();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::updatePlaybackState(
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms)
{
    duration_ms_ = duration_ms;
    if (viewing_historical_frame_ && state == LNPLAYER_STATE_PAUSED &&
        video_frame_history_index_ >= 0 &&
        video_frame_history_index_ < static_cast<int>(video_frame_history_.size()))
        position_ms = video_frame_history_[video_frame_history_index_].pts_ms;
    else if (state != LNPLAYER_STATE_PAUSED)
        viewing_historical_frame_ = false;
    current_position_ms_ = position_ms;

    const bool was_playing = is_playing_;
    is_playing_ = state == LNPLAYER_STATE_PLAYING;
    if (mode_ == PlayerMode::Video && has_video_)
    {
        if (is_playing_)
        {
            if (!was_playing)
                video_end_of_stream_ = false;
            if (!video_timer_->isActive())
                video_timer_->start();
        }
        else
        {
            video_timer_->stop();
            presentPendingVideoFrame();
        }
    }

    const QString duration_text = duration_ms < 0
        ? QStringLiteral("--:--")
        : format_position(duration_ms);
    position_label_->setText(
        QStringLiteral("%1 / %2")
            .arg(format_position(position_ms), duration_text));

    if (!slider_dragging_ && duration_ms > 0)
    {
        const int value = static_cast<int>(
            std::clamp<int64_t>(position_ms * 1000 / duration_ms, 0, 1000));
        seek_slider_->setValue(value);
    }

    switch (state)
    {
    case LNPLAYER_STATE_OPENING:
        status_label_->setText(QStringLiteral("正在打开"));
        break;
    case LNPLAYER_STATE_READY:
        status_label_->setText(QStringLiteral("已就绪"));
        break;
    case LNPLAYER_STATE_PLAYING:
        status_label_->setText(QStringLiteral("正在播放"));
        break;
    case LNPLAYER_STATE_PAUSED:
        status_label_->setText(QStringLiteral("已暂停"));
        break;
    case LNPLAYER_STATE_ENDED:
        status_label_->setText(QStringLiteral("播放结束"));
        break;
    case LNPLAYER_STATE_ERROR:
        status_label_->setText(QStringLiteral("播放错误"));
        break;
    case LNPLAYER_STATE_CLOSING:
        status_label_->setText(QStringLiteral("正在关闭"));
        break;
    case LNPLAYER_STATE_IDLE:
    case LNPLAYER_STATE_SEEKING:
    default:
        break;
    }
}
