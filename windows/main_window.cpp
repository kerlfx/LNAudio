#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"

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
#include <QFontComboBox>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QDropEvent>
#include <QEvent>
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
#include <QShortcut>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
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
    , frame_worker_(mode == PlayerMode::Video ? new QObject() : nullptr)
    , frame_thread_(mode == PlayerMode::Video ? new QThread(this) : nullptr)
    , library_worker_(mode == PlayerMode::Audio ? new QObject() : nullptr)
    , library_thread_(mode == PlayerMode::Audio ? new QThread(this) : nullptr)
    , snapshot_timer_(new QTimer(this))
    , video_timer_(new QTimer(this))
    , device_timer_(new QTimer(this))
    , fullscreen_hide_timer_(new QTimer(this))
    , osd_timer_(new QTimer(this))
{
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
        playback_rate_ = std::clamp(
            settings.value(QStringLiteral("Video/PlaybackRate"), 1.0).toFloat(),
            0.25f, 4.0f);
        playlist_panel_visible_ = settings.value(
            QStringLiteral("Video/UI/PlaylistVisible"), true).toBool();
        fullscreen_hide_delay_ms_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/FullscreenHideDelayMs"), 2000).toInt(),
            0, 10000);
        subtitle_font_family_ = settings.value(
            QStringLiteral("Video/Subtitle/FontFamily"), QStringLiteral("Segoe UI")).toString();
        subtitle_color_ = settings.value(
            QStringLiteral("Video/Subtitle/Color"), QStringLiteral("#ffffff")).toString();
        subtitle_font_size_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/FontSize"), 0).toInt(), 0, 96);
        subtitle_vertical_position_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/VerticalPosition"), 80).toInt(), 60, 80);
    }
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

    if (mode_ == PlayerMode::Video)
    {
        auto* video_menu = menuBar()->addMenu(QStringLiteral("视频"));
        auto* playback_info_action = video_menu->addAction(QStringLiteral("播放信息..."));
        connect(playback_info_action, &QAction::triggered, this, [this] { showPlaybackInfo(); });
        auto* screenshot_action = video_menu->addAction(QStringLiteral("截图..."));
        connect(screenshot_action, &QAction::triggered, this, [this] { takeScreenshot(); });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* playlist_menu = menuBar()->addMenu(QStringLiteral("播放列表"));
        playlist_visibility_action_ = playlist_menu->addAction(QStringLiteral("显示播放列表"));
        playlist_visibility_action_->setCheckable(true);
        playlist_visibility_action_->setChecked(playlist_panel_visible_);
        connect(playlist_visibility_action_, &QAction::toggled, this, [this](bool visible)
        {
            playlist_panel_visible_ = visible;
            if (playlist_panel_)
                playlist_panel_->setVisible(visible);
            QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
            settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), visible);
            settings.sync();
            if (visible && isFullScreen())
                showFullscreenControls();
        });
        auto* settings_action = playlist_menu->addAction(QStringLiteral("视频设置..."));
        connect(settings_action, &QAction::triggered, this, [this] { openVideoSettings(); });
        auto* clear_playlist_action = playlist_menu->addAction(QStringLiteral("清空播放列表"));
        connect(clear_playlist_action, &QAction::triggered, this, [this]
        {
            clearPlaylist();
        });
    }

    if (menuBar())
    {
        menuBar()->installEventFilter(this);
        const auto menu_widgets = menuBar()->findChildren<QWidget*>();
        for (QWidget* menu_widget : menu_widgets)
        {
            menu_widget->setMouseTracking(true);
            menu_widget->installEventFilter(this);
        }
    }

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);

    if (mode_ == PlayerMode::Video)
    {
        video_surface_ = new VideoSurface(content);
        osd_label_ = new QLabel(video_surface_);
        osd_label_->setAlignment(Qt::AlignCenter);
        osd_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        osd_label_->setStyleSheet(QStringLiteral(
            "QLabel { color: white; background: rgba(0, 0, 0, 185); "
            "border-radius: 8px; padding: 10px 18px; font-size: 18px; }"));
        osd_label_->hide();
        osd_timer_->setSingleShot(true);
        connect(osd_timer_, &QTimer::timeout, osd_label_, &QWidget::hide);
        {
            QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
            video_brightness_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Brightness"), 0.0).toFloat(),
                -1.0f, 1.0f);
            video_contrast_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Contrast"), 1.0).toFloat(),
                0.0f, 2.0f);
            video_saturation_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Saturation"), 1.0).toFloat(),
                0.0f, 2.0f);
            video_hue_degrees_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/HueDegrees"), 0.0).toFloat(),
                -180.0f, 180.0f);
            video_sharpness_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Sharpness"), 0.0).toFloat(),
                0.0f, 2.0f);
            video_denoise_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Denoise"), 0.0).toFloat(),
                0.0f, 1.0f);
            video_grayscale_ = settings.value(
                QStringLiteral("Video/Filter/Grayscale"), false).toBool();
            video_aspect_ratio_override_ = settings.value(
                QStringLiteral("Video/Display/AspectRatio"), 0.0).toFloat();
            if (video_aspect_ratio_override_ != 16.0f / 9.0f &&
                video_aspect_ratio_override_ != 4.0f / 3.0f)
                video_aspect_ratio_override_ = 0.0f;
            video_stretch_ = settings.value(
                QStringLiteral("Video/Display/Stretch"), false).toBool();
            playlist_panel_visible_ = settings.value(
                QStringLiteral("Video/UI/PlaylistVisible"), true).toBool();
        }
        video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
        video_surface_->set_stretch(video_stretch_);
        video_surface_->set_video_filter(
            video_brightness_, video_contrast_, video_saturation_,
            video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);
        playlist_widget_ = new QListWidget(content);
        playlist_widget_->setMaximumWidth(280);
        playlist_widget_->setDragDropMode(QAbstractItemView::InternalMove);
        playlist_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        playlist_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
        playlist_widget_->setDefaultDropAction(Qt::MoveAction);
        playlist_panel_ = new QWidget(content);
        auto* playlist_panel_layout = new QVBoxLayout(playlist_panel_);
        playlist_panel_layout->setContentsMargins(0, 0, 0, 0);
        auto* playlist_actions = new QHBoxLayout();
        auto* add_to_queue = new QPushButton(QStringLiteral("添加"), playlist_panel_);
        auto* remove_from_queue = new QPushButton(QStringLiteral("移除"), playlist_panel_);
        auto* clear_queue = new QPushButton(QStringLiteral("清空"), playlist_panel_);
        auto* move_up = new QPushButton(QStringLiteral("上移"), playlist_panel_);
        auto* move_down = new QPushButton(QStringLiteral("下移"), playlist_panel_);
        auto* shuffle_button = new QPushButton(QStringLiteral("随机：关"), playlist_panel_);
        auto* repeat_button = new QPushButton(QStringLiteral("循环：关"), playlist_panel_);
        shuffle_button_ = shuffle_button;
        repeat_button_ = repeat_button;
        shuffle_button->setCheckable(true);
        playlist_actions->addWidget(add_to_queue);
        playlist_actions->addWidget(remove_from_queue);
        playlist_actions->addWidget(clear_queue);
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
                QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));
            QList<QUrl> urls;
            for (const QString& file : files)
                urls.append(QUrl::fromLocalFile(file));
            appendPlaylistFiles(urls);
        });
        connect(remove_from_queue, &QPushButton::clicked, this,
            [this] { removeSelectedPlaylistItem(); });
        connect(clear_queue, &QPushButton::clicked, this,
            [this] { clearPlaylist(); });
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
        connect(playlist_widget_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& point)
        {
            QListWidgetItem* item = playlist_widget_->itemAt(point);
            if (item && !item->isSelected())
            {
                playlist_widget_->clearSelection();
                item->setSelected(true);
                playlist_widget_->setCurrentItem(item);
            }
            QMenu menu(playlist_widget_);
            QAction* play_selected = menu.addAction(QStringLiteral("播放"));
            QAction* remove_selected = menu.addAction(QStringLiteral("移除选中项"));
            QAction* clear_all = menu.addAction(QStringLiteral("清空播放列表"));
            QAction* selected = menu.exec(playlist_widget_->mapToGlobal(point));
            if (selected == play_selected && item)
                openPlaylistIndex(playlist_widget_->row(item));
            else if (selected == remove_selected)
                removeSelectedPlaylistItem();
            else if (selected == clear_all)
                clearPlaylist();
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
            menu.setMouseTracking(true);
            menu.installEventFilter(this);
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
            QAction* aspect_actions[] = {fit, ratio_16_9, ratio_4_3, stretch};
            for (QAction* action : aspect_actions)
                action->setCheckable(true);
            fit->setChecked(video_aspect_ratio_override_ == 0.0f && !video_stretch_);
            ratio_16_9->setChecked(
                std::abs(video_aspect_ratio_override_ - 16.0f / 9.0f) < 0.001f);
            ratio_4_3->setChecked(
                std::abs(video_aspect_ratio_override_ - 4.0f / 3.0f) < 0.001f);
            stretch->setChecked(video_stretch_);

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
                const bool enabled = subtitles_enabled_;
                QMetaObject::invokeMethod(command_worker_, [this, enabled]
                {
                    if (player_)
                        lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
                }, Qt::QueuedConnection);
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
            else if (selected == fit || selected == ratio_16_9 ||
                selected == ratio_4_3 || selected == stretch)
            {
                video_stretch_ = selected == stretch;
                video_aspect_ratio_override_ = selected == ratio_16_9
                    ? 16.0f / 9.0f
                    : selected == ratio_4_3 ? 4.0f / 3.0f : 0.0f;
                video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
                video_surface_->set_stretch(video_stretch_);
                QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
                settings.setValue(
                    QStringLiteral("Video/Display/AspectRatio"),
                    video_aspect_ratio_override_);
                settings.setValue(QStringLiteral("Video/Display/Stretch"), video_stretch_);
                settings.sync();
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
                QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
                settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
                settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
                settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontFamily"), subtitle_font_family_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontSize"), subtitle_font_size_);
    settings.setValue(QStringLiteral("Video/Subtitle/Color"), subtitle_color_);
    settings.setValue(QStringLiteral("Video/Subtitle/VerticalPosition"), subtitle_vertical_position_);
                settings.setValue(QStringLiteral("Video/Filter/HueDegrees"), video_hue_degrees_);
                settings.setValue(QStringLiteral("Video/Filter/Sharpness"), video_sharpness_);
                settings.setValue(QStringLiteral("Video/Filter/Denoise"), video_denoise_);
                settings.setValue(QStringLiteral("Video/Filter/Grayscale"), video_grayscale_);
                settings.sync();
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                statusBar()->showMessage(QStringLiteral("视频滤镜已更新"));
            }
        });
        auto* video_layout = new QHBoxLayout();
        video_layout->addWidget(video_surface_, 1);
        playlist_panel_->setVisible(playlist_panel_visible_);
        video_layout->addWidget(playlist_panel_);
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
    {
        QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
        const int saved_volume = std::clamp(
            settings.value(QStringLiteral("Playback/Volume"), 1000).toInt(), 0, 1000);
        muted_volume_ = std::clamp(
            settings.value(QStringLiteral("Playback/MutedVolume"), 1000).toInt(), 1, 1000);
        volume_slider_->setValue(saved_volume);
        if (saved_volume > 0)
            muted_volume_ = saved_volume;
    }
    volume_slider_->setMaximumWidth(160);

    auto* seek_layout = new QHBoxLayout();
    seek_layout->addWidget(position_label_);
    seek_layout->addWidget(seek_slider_, 1);
    control_panel_ = new QWidget(content);
    auto* control_panel_layout = new QVBoxLayout(control_panel_);
    control_panel_layout->setContentsMargins(0, 0, 0, 0);
    control_panel_layout->addLayout(seek_layout);

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
    control_panel_layout->addLayout(control_layout);
    control_panel_layout->addWidget(status_label_);
    layout->addWidget(control_panel_);
    setCentralWidget(content);

    fullscreen_hide_timer_->setSingleShot(true);
    fullscreen_hide_timer_->setInterval(fullscreen_hide_delay_ms_);
    connect(fullscreen_hide_timer_, &QTimer::timeout, this, [this]
    {
        if (!isFullScreen() || !control_panel_)
            return;
        control_panel_->hide();
        fullscreen_controls_visible_ = false;
        if (menuBar())
            menuBar()->hide();
        if (playlist_panel_)
            playlist_panel_->hide();
        setCursor(Qt::BlankCursor);
    });
    if (video_surface_)
    {
        video_surface_->setMouseTracking(true);
        video_surface_->installEventFilter(this);
    }
    control_panel_->setMouseTracking(true);
    control_panel_->installEventFilter(this);
    const auto control_children = control_panel_->findChildren<QWidget*>();
    for (QWidget* child : control_children)
    {
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }

    if (mode_ == PlayerMode::Video)
    {
        auto bind_shortcut = [this](const QString& id, const QString& label,
            int key, std::function<void()> action)
        {
            const QKeySequence default_sequence(key);
            QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
            const QString setting_key = QStringLiteral("Video/Shortcuts/") + id;
            const QKeySequence sequence = QKeySequence::fromString(
                settings.value(setting_key, default_sequence.toString()).toString());
            auto* shortcut = new QShortcut(sequence, this);
            shortcut->setContext(Qt::WindowShortcut);
            shortcut->setProperty("commandLabel", label);
            shortcut->setProperty("defaultSequence", default_sequence.toString());
            shortcuts_.insert(id, shortcut);
            connect(shortcut, &QShortcut::activated, this,
                [action = std::move(action)]
            {
                QWidget* focus = QApplication::focusWidget();
                if (qobject_cast<QLineEdit*>(focus) ||
                    qobject_cast<QAbstractItemView*>(focus))
                    return;
                action();
            });
        };
        bind_shortcut(QStringLiteral("play_pause"), QStringLiteral("播放/暂停"), Qt::Key_Space, [this] { togglePlayback(); });
        bind_shortcut(QStringLiteral("fullscreen"), QStringLiteral("全屏切换"), Qt::Key_F11, [this]
        {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
        });
        bind_shortcut(QStringLiteral("exit_fullscreen"), QStringLiteral("退出全屏"), Qt::Key_Escape, [this]
        {
            if (isFullScreen())
                showNormal();
        });
        bind_shortcut(QStringLiteral("seek_backward"), QStringLiteral("后退 5 秒"), Qt::Key_Left, [this] { seekRelative(-5000); });
        bind_shortcut(QStringLiteral("seek_forward"), QStringLiteral("前进 5 秒"), Qt::Key_Right, [this] { seekRelative(5000); });
        bind_shortcut(QStringLiteral("frame_backward"), QStringLiteral("上一帧"), Qt::Key_Comma, [this] { stepVideoFrame(true); });
        bind_shortcut(QStringLiteral("frame_forward"), QStringLiteral("下一帧"), Qt::Key_Period, [this] { stepVideoFrame(); });
        bind_shortcut(QStringLiteral("volume_up"), QStringLiteral("音量增加"), Qt::Key_Up, [this]
        {
            volume_slider_->setValue(std::min(1000, volume_slider_->value() + 50));
        });
        bind_shortcut(QStringLiteral("volume_down"), QStringLiteral("音量降低"), Qt::Key_Down, [this]
        {
            volume_slider_->setValue(std::max(0, volume_slider_->value() - 50));
        });
        bind_shortcut(QStringLiteral("previous_item"), QStringLiteral("上一项"), Qt::Key_PageUp, [this] { playPrevious(); });
        bind_shortcut(QStringLiteral("next_item"), QStringLiteral("下一项"), Qt::Key_PageDown, [this] { playNext(); });
        bind_shortcut(QStringLiteral("seek_backward_large"), QStringLiteral("后退 30 秒"), Qt::CTRL | Qt::Key_Left, [this] { seekRelative(-30000); });
        bind_shortcut(QStringLiteral("seek_forward_large"), QStringLiteral("前进 30 秒"), Qt::CTRL | Qt::Key_Right, [this] { seekRelative(30000); });
        bind_shortcut(QStringLiteral("open_file"), QStringLiteral("打开文件"), Qt::Key_F3, [this] { openFile(); });
        bind_shortcut(QStringLiteral("open_settings"), QStringLiteral("打开设置"), Qt::Key_F5, [this] { openVideoSettings(); });
        bind_shortcut(QStringLiteral("playback_info"), QStringLiteral("播放信息"), Qt::Key_F7, [this] { showPlaybackInfo(); });
        bind_shortcut(QStringLiteral("toggle_playlist"), QStringLiteral("显示/隐藏播放列表"), Qt::Key_F6, [this]
        {
            if (playlist_visibility_action_)
                playlist_visibility_action_->setChecked(!playlist_visibility_action_->isChecked());
        });
        bind_shortcut(QStringLiteral("speed_down"), QStringLiteral("降低播放速度"), Qt::Key_X, [this]
        {
            setPlaybackRate(std::max(0.25f, playback_rate_ - 0.25f));
        });
        bind_shortcut(QStringLiteral("speed_up"), QStringLiteral("提高播放速度"), Qt::Key_C, [this]
        {
            setPlaybackRate(std::min(4.0f, playback_rate_ + 0.25f));
        });
        bind_shortcut(QStringLiteral("speed_reset"), QStringLiteral("恢复正常速度"), Qt::Key_Z, [this] { setPlaybackRate(1.0f); });
        bind_shortcut(QStringLiteral("screenshot"), QStringLiteral("截图"), Qt::Key_K, [this] { takeScreenshot(); });
        bind_shortcut(QStringLiteral("mute"), QStringLiteral("静音切换"), Qt::Key_M, [this]
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
    if (frame_worker_)
    {
        frame_worker_->moveToThread(frame_thread_);
        frame_thread_->start();
    }
    if (library_worker_)
    {
        library_worker_->moveToThread(library_thread_);
        library_thread_->start();
    }

    const uint32_t initial_volume = static_cast<uint32_t>(volume_slider_->value());
    const float initial_playback_rate = playback_rate_;
    void* initial_video_device = mode_ == PlayerMode::Video && video_surface_
        ? video_surface_->native_device()
        : nullptr;
    video_device_identity_ = initial_video_device;
    QMetaObject::invokeMethod(
        command_worker_,
        [this, initial_volume, initial_playback_rate, initial_video_device]
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
        if (result == LNPLAYER_RESULT_OK)
        {
            lnplayer_set_volume(player_, initial_volume);
            if (mode_ == PlayerMode::Video)
                lnplayer_set_playback_rate(player_, initial_playback_rate);
        }

        if (initial_video_device)
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                const lnplayer_result_t device_result =
                    lnplayer_windows_set_d3d11_device(player_, initial_video_device);
                if (device_result != LNPLAYER_RESULT_OK)
                {
                    static_cast<ID3D11Device*>(initial_video_device)->Release();
                    QMetaObject::invokeMethod(this, [this]
                    {
                        video_device_identity_ = nullptr;
                    }, Qt::QueuedConnection);
                }
            }
            else
            {
                static_cast<ID3D11Device*>(initial_video_device)->Release();
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
        QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
        settings.setValue(QStringLiteral("Playback/Volume"), value);
        if (value > 0)
        {
            muted_volume_ = value;
            settings.setValue(QStringLiteral("Playback/MutedVolume"), value);
        }
        settings.sync();

        showOsd(value == 0 ? QStringLiteral("静音")
            : QStringLiteral("音量 %1%").arg(value / 10));
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
        if (video_seek_pending_)
            return;
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
        seek_slider_->setEnabled(false);
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
                    seek_slider_->setEnabled(duration_ms_ > 0);
                    requestVideoFrame();
                }, Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);
    });

    snapshot_timer_->setInterval(250);
    connect(snapshot_timer_, &QTimer::timeout, this, &MainWindow::refreshSnapshot);

    video_timer_->setInterval(1);
    video_timer_->setTimerType(Qt::PreciseTimer);
    connect(video_timer_, &QTimer::timeout, this, [this]
    {
        presentPendingVideoFrame();
        if (is_playing_ && has_video_ && !video_seek_pending_ &&
            !video_frame_pending_ && !pending_video_frame_valid_)
            requestVideoFrame();
    });

    device_timer_->setInterval(250);
    connect(device_timer_, &QTimer::timeout, this, [this]
    {
        if (!video_surface_)
            return;

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
    });
    if (mode_ == PlayerMode::Video)
        device_timer_->start();

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
    device_timer_->stop();

    if (frame_thread_ && frame_thread_->isRunning())
    {
        frame_thread_->quit();
        frame_thread_->wait();
    }
    delete frame_worker_;

    if (library_thread_ && library_thread_->isRunning())
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
    const QString renderer_state = video_surface_ && video_surface_->has_renderer()
        ? QStringLiteral("D3D11 渲染器已初始化")
        : QStringLiteral("D3D11 渲染器不可用");

    const QString details = QStringLiteral(
        "媒体：%1\n路径：%2\n状态：%3\n位置：%4 / %5\n"
        "视频：%6\n音轨：%7\n播放速度：%8x\n音量：%9%\n"
        "字幕偏移：%10 ms\n渲染：%11")
        .arg(media_title_label_->text())
        .arg(media_path.isEmpty() ? QStringLiteral("无") : media_path)
        .arg(status_label_->text())
        .arg(position)
        .arg(duration)
        .arg(video_state)
        .arg(audio_state)
        .arg(playback_rate_, 0, 'f', 2)
        .arg(volume_slider_->value() / 10)
        .arg(subtitle_offset_ms_)
        .arg(renderer_state);
    QMessageBox::information(this, QStringLiteral("播放信息"), details);
}

void MainWindow::showOsd(const QString& message)
{
    if (!osd_label_ || !video_surface_ || message.isEmpty())
        return;

    osd_label_->setText(message);
    osd_label_->adjustSize();
    osd_label_->move(
        (video_surface_->width() - osd_label_->width()) / 2,
        (video_surface_->height() - osd_label_->height()) / 2);
    osd_label_->raise();
    osd_label_->show();
    osd_timer_->start(1500);
}

void MainWindow::showFullscreenControls()
{
    if (!isFullScreen() || !control_panel_)
        return;

    unsetCursor();
    control_panel_->show();
    if (menuBar())
        menuBar()->show();
    if (playlist_panel_)
        playlist_panel_->setVisible(playlist_panel_visible_);
    fullscreen_controls_visible_ = true;
    if (fullscreen_hide_delay_ms_ > 0)
        fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
    else
        fullscreen_hide_timer_->stop();
}

void MainWindow::updateFullscreenUi()
{
    if (!isFullScreen())
    {
        fullscreen_hide_timer_->stop();
        unsetCursor();
        if (control_panel_)
            control_panel_->show();
        if (menuBar())
            menuBar()->show();
        if (playlist_panel_)
            playlist_panel_->setVisible(playlist_panel_visible_);
        fullscreen_controls_visible_ = true;
        return;
    }

    if (control_panel_)
        control_panel_->show();
    if (menuBar())
        menuBar()->show();
    if (playlist_panel_)
        playlist_panel_->setVisible(playlist_panel_visible_);
    fullscreen_controls_visible_ = true;
    if (fullscreen_hide_delay_ms_ > 0)
        fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
    else
        fullscreen_hide_timer_->stop();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (isFullScreen())
    {
        QWidget* watched_widget = qobject_cast<QWidget*>(watched);
        bool is_menu_widget = qobject_cast<QMenu*>(watched) != nullptr;
        for (QWidget* parent = watched_widget ? watched_widget->parentWidget() : nullptr;
            parent && !is_menu_widget; parent = parent->parentWidget())
            is_menu_widget = qobject_cast<QMenu*>(parent) != nullptr;
        const bool is_control_widget = watched == control_panel_ ||
            (control_panel_ && watched_widget && control_panel_->isAncestorOf(watched_widget)) ||
            watched == menuBar() || (menuBar() && watched_widget && menuBar()->isAncestorOf(watched_widget)) ||
            is_menu_widget;
        if (watched == video_surface_ && event->type() == QEvent::Resize && osd_label_ && osd_label_->isVisible())
        {
            osd_label_->adjustSize();
            osd_label_->move(
                (video_surface_->width() - osd_label_->width()) / 2,
                (video_surface_->height() - osd_label_->height()) / 2);
        }
        if (event->type() == QEvent::Show && is_menu_widget)
        {
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Hide && is_menu_widget)
        {
            if (fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
        else if (event->type() == QEvent::MouseMove)
        {
            if (is_control_widget)
            {
                unsetCursor();
                fullscreen_hide_timer_->stop();
            }
            else
            {
                showFullscreenControls();
            }
        }
        else if (event->type() == QEvent::Enter && is_control_widget)
        {
            unsetCursor();
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Leave && is_control_widget)
        {
            if (fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateFullscreenUi();
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
        QStringLiteral("媒体文件 (*.mp3 *.flac *.wav *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));

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
        const QFileInfo info(url.toLocalFile());
        auto* item = new QListWidgetItem(info.fileName(), playlist_widget_);
        item->setData(Qt::UserRole, url.toLocalFile());
        item->setToolTip(QStringLiteral("%1\n%2 bytes")
            .arg(info.absoluteFilePath())
            .arg(info.exists() ? info.size() : 0));
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

void MainWindow::clearPlaylist()
{
    playlist_.clear();
    playlist_index_ = -1;
    shuffle_order_.clear();
    playing_playlist_item_ = nullptr;
    playlist_open_pending_ = false;
    video_seek_pending_ = false;
    video_end_of_stream_ = false;
    has_video_ = false;
    has_audio_ = false;
    is_playing_ = false;
    duration_ms_ = -1;
    current_position_ms_ = 0;
    if (seek_slider_)
    {
        seek_slider_->setEnabled(false);
        seek_slider_->setValue(0);
    }
    video_first_frame_received_ = false;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    pending_hardware_frame_owner_.reset();
    pending_hardware_texture_ = nullptr;
    video_timer_->stop();

    if (playlist_widget_)
        playlist_widget_->clear();
    if (video_surface_)
        video_surface_->set_frame(QImage());
    media_title_label_->setText(QStringLiteral("未打开媒体"));
    position_label_->setText(QStringLiteral("00:00 / --:--"));
    status_label_->setText(QStringLiteral("播放列表已清空"));
    statusBar()->showMessage(QStringLiteral("播放列表已清空"));

    QMetaObject::invokeMethod(command_worker_, [this]
    {
        if (player_)
            lnplayer_close(player_);
    }, Qt::QueuedConnection);
    saveVideoPlaylist();
}

void MainWindow::openVideoSettings()
{
    if (mode_ != PlayerMode::Video)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("LNPlayer 视频设置"));
    dialog.setMinimumSize(560, 420);
    auto* root = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);

    auto* interface_page = new QWidget(tabs);
    auto* interface_form = new QFormLayout(interface_page);
    auto* hide_delay = new QSpinBox(interface_page);
    hide_delay->setRange(0, 10);
    hide_delay->setSuffix(QStringLiteral(" 秒"));
    hide_delay->setSpecialValueText(QStringLiteral("关闭"));
    hide_delay->setValue(fullscreen_hide_delay_ms_ / 1000);
    auto* show_playlist = new QCheckBox(QStringLiteral("显示播放列表侧栏"), interface_page);
    show_playlist->setChecked(playlist_panel_visible_);
    interface_form->addRow(QStringLiteral("全屏控制栏自动隐藏"), hide_delay);
    interface_form->addRow(show_playlist);
    tabs->addTab(interface_page, QStringLiteral("界面"));

    auto* video_page = new QWidget(tabs);
    auto* video_form = new QFormLayout(video_page);
    auto* aspect = new QComboBox(video_page);
    aspect->addItem(QStringLiteral("原始比例"), 0);
    aspect->addItem(QStringLiteral("16:9"), 1);
    aspect->addItem(QStringLiteral("4:3"), 2);
    aspect->addItem(QStringLiteral("拉伸填充"), 3);
    const int aspect_index = video_stretch_ ? 3
        : std::abs(video_aspect_ratio_override_ - 16.0f / 9.0f) < 0.001f ? 1
        : std::abs(video_aspect_ratio_override_ - 4.0f / 3.0f) < 0.001f ? 2 : 0;
    aspect->setCurrentIndex(aspect_index);

    auto make_filter_spin = [&video_page](double value, double minimum, double maximum, double step)
    {
        auto* spin = new QDoubleSpinBox(video_page);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    };
    auto* brightness = make_filter_spin(video_brightness_, -1.0, 1.0, 0.05);
    auto* contrast = make_filter_spin(video_contrast_, 0.0, 2.0, 0.05);
    auto* saturation = make_filter_spin(video_saturation_, 0.0, 2.0, 0.05);
    video_form->addRow(QStringLiteral("画面比例"), aspect);
    video_form->addRow(QStringLiteral("亮度"), brightness);
    video_form->addRow(QStringLiteral("对比度"), contrast);
    video_form->addRow(QStringLiteral("饱和度"), saturation);
    tabs->addTab(video_page, QStringLiteral("视频"));

    auto* subtitle_page = new QWidget(tabs);
    auto* subtitle_form = new QFormLayout(subtitle_page);
    auto* subtitle_font = new QFontComboBox(subtitle_page);
    subtitle_font->setCurrentFont(QFont(subtitle_font_family_));
    auto* subtitle_size = new QSpinBox(subtitle_page);
    subtitle_size->setRange(0, 96);
    subtitle_size->setSpecialValueText(QStringLiteral("自动"));
    subtitle_size->setSuffix(QStringLiteral(" px"));
    subtitle_size->setValue(subtitle_font_size_);
    auto* subtitle_color = new QComboBox(subtitle_page);
    subtitle_color->addItem(QStringLiteral("白色"), QStringLiteral("#ffffff"));
    subtitle_color->addItem(QStringLiteral("黄色"), QStringLiteral("#ffff00"));
    subtitle_color->addItem(QStringLiteral("青色"), QStringLiteral("#00ffff"));
    subtitle_color->addItem(QStringLiteral("绿色"), QStringLiteral("#00ff00"));
    int subtitle_color_index = subtitle_color->findData(subtitle_color_);
    subtitle_color->setCurrentIndex(subtitle_color_index >= 0 ? subtitle_color_index : 0);
    auto* subtitle_position = new QSpinBox(subtitle_page);
    subtitle_position->setRange(60, 80);
    subtitle_position->setSuffix(QStringLiteral(" %"));
    subtitle_position->setValue(subtitle_vertical_position_);
    subtitle_form->addRow(QStringLiteral("字体"), subtitle_font);
    subtitle_form->addRow(QStringLiteral("字号"), subtitle_size);
    subtitle_form->addRow(QStringLiteral("颜色"), subtitle_color);
    subtitle_form->addRow(QStringLiteral("垂直位置"), subtitle_position);
    tabs->addTab(subtitle_page, QStringLiteral("字幕"));

    auto* shortcut_page = new QWidget(tabs);
    auto* shortcut_layout = new QVBoxLayout(shortcut_page);
    auto* shortcut_form = new QFormLayout();
    QHash<QString, QKeySequenceEdit*> shortcut_edits;
    for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it)
    {
        auto* edit = new QKeySequenceEdit(it.value()->key(), shortcut_page);
        shortcut_edits.insert(it.key(), edit);
        shortcut_form->addRow(it.value()->property("commandLabel").toString(), edit);
    }
    auto* shortcut_form_widget = new QWidget(shortcut_page);
    shortcut_form_widget->setLayout(shortcut_form);
    auto* shortcut_scroll = new QScrollArea(shortcut_page);
    shortcut_scroll->setWidgetResizable(true);
    shortcut_scroll->setWidget(shortcut_form_widget);
    shortcut_layout->addWidget(shortcut_scroll);
    tabs->addTab(shortcut_page, QStringLiteral("快捷键"));

    root->addWidget(tabs, 1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* defaults = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    root->addWidget(buttons);

    connect(defaults, &QPushButton::clicked, &dialog, [=]
    {
        hide_delay->setValue(2);
        show_playlist->setChecked(true);
        aspect->setCurrentIndex(0);
        brightness->setValue(0.0);
        contrast->setValue(1.0);
        saturation->setValue(1.0);
        subtitle_font->setCurrentFont(QFont(QStringLiteral("Segoe UI")));
        subtitle_size->setValue(0);
        subtitle_color->setCurrentIndex(0);
        subtitle_position->setValue(80);
        for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it)
        {
            const QKeySequence default_sequence = QKeySequence::fromString(
                it.value()->property("defaultSequence").toString());
            shortcut_edits.value(it.key())->setKeySequence(default_sequence);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this]
    {
        QList<QPair<QString, QKeySequence>> candidates;
        for (auto it = shortcut_edits.cbegin(); it != shortcut_edits.cend(); ++it)
        {
            const QKeySequence sequence = it.value()->keySequence();
            if (sequence.isEmpty())
                continue;
            for (const auto& candidate : candidates)
            {
                if (candidate.second.matches(sequence) != QKeySequence::NoMatch ||
                    sequence.matches(candidate.second) != QKeySequence::NoMatch)
                {
                    QMessageBox::warning(&dialog, QStringLiteral("快捷键冲突"),
                        QStringLiteral("快捷键与“%1”重复，请修改后再保存。")
                            .arg(shortcuts_.value(candidate.first)->property("commandLabel").toString()));
                    return;
                }
            }
            candidates.append(qMakePair(it.key(), sequence));
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    fullscreen_hide_delay_ms_ = hide_delay->value() * 1000;
    fullscreen_hide_timer_->setInterval(fullscreen_hide_delay_ms_);
    playlist_panel_visible_ = show_playlist->isChecked();
    if (playlist_visibility_action_)
        playlist_visibility_action_->setChecked(playlist_panel_visible_);
    if (playlist_panel_)
        playlist_panel_->setVisible(playlist_panel_visible_);

    const int selected_aspect = aspect->currentData().toInt();
    video_stretch_ = selected_aspect == 3;
    video_aspect_ratio_override_ = selected_aspect == 1 ? 16.0f / 9.0f
        : selected_aspect == 2 ? 4.0f / 3.0f : 0.0f;
    video_brightness_ = static_cast<float>(brightness->value());
    video_contrast_ = static_cast<float>(contrast->value());
    video_saturation_ = static_cast<float>(saturation->value());
    subtitle_font_family_ = subtitle_font->currentFont().family();
    subtitle_font_size_ = subtitle_size->value();
    subtitle_color_ = subtitle_color->currentData().toString();
    subtitle_vertical_position_ = subtitle_position->value();
    video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
    video_surface_->set_stretch(video_stretch_);
    video_surface_->set_video_filter(
        video_brightness_, video_contrast_, video_saturation_,
        video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);

    QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
    settings.setValue(QStringLiteral("Video/UI/FullscreenHideDelayMs"), fullscreen_hide_delay_ms_);
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it)
    {
        const QKeySequence sequence = shortcut_edits.value(it.key())->keySequence();
        it.value()->setKey(sequence);
        settings.setValue(
            QStringLiteral("Video/Shortcuts/") + it.key(),
            sequence.toString(QKeySequence::PortableText));
    }
    settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), playlist_panel_visible_);
    settings.setValue(QStringLiteral("Video/Display/AspectRatio"), video_aspect_ratio_override_);
    settings.setValue(QStringLiteral("Video/Display/Stretch"), video_stretch_);
    settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
    settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
    settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
    settings.sync();

    if (isFullScreen())
    {
        showFullscreenControls();
        if (fullscreen_hide_delay_ms_ == 0)
        {
            control_panel_->show();
            menuBar()->show();
        }
    }
    pending_video_frame_valid_ = false;
    requestVideoFrame();
}

void MainWindow::removeSelectedPlaylistItem()
{
    if (!playlist_widget_)
        return;

    std::vector<int> rows;
    const auto selected_items = playlist_widget_->selectedItems();
    for (QListWidgetItem* item : selected_items)
        rows.push_back(playlist_widget_->row(item));
    if (rows.empty() && playlist_widget_->currentRow() >= 0)
        rows.push_back(playlist_widget_->currentRow());
    if (rows.empty())
        return;

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    const int old_current = playlist_index_;
    const bool removing_current = std::binary_search(rows.begin(), rows.end(), old_current);
    const int first_removed = rows.front();
    const int removed_before_current = static_cast<int>(
        std::count_if(rows.begin(), rows.end(), [old_current](int row)
        {
            return row < old_current;
        }));

    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        if (*it >= 0 && *it < playlist_.size())
            playlist_.removeAt(*it);
    }

    if (removing_current)
    {
        if (playlist_.isEmpty())
        {
            clearPlaylist();
            return;
        }
        openPlaylistIndex(std::min(first_removed, static_cast<int>(playlist_.size()) - 1));
        return;
    }

    if (old_current >= 0)
        playlist_index_ = old_current - removed_before_current;
    resetShuffleOrder();
    rebuildPlaylistWidget();
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

    subtitle_manager_.clear();
    viewing_historical_frame_ = false;
    video_seek_pending_ = false;
    pending_embedded_subtitle_.clear();
    pending_embedded_subtitle_bitmap_ = {};
    pending_video_display_aspect_ratio_ = 0.0f;
    pending_hdr_transfer_ = 0;
    pending_color_primaries_ = 0;
    pending_hdr_peak_luminance_ = 1000.0f;
    pending_video_ycbcr_matrix_ = 0;
    pending_video_ycbcr_full_range_ = 0;
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

    const QByteArray path_utf8 = path.toUtf8();
    QMetaObject::invokeMethod(command_worker_, [this, path, path_utf8]
    {
        const lnplayer_result_t result = player_
            ? lnplayer_set_external_subtitle(player_, path_utf8.constData())
            : LNPLAYER_RESULT_INVALID_HANDLE;
        if (result != LNPLAYER_RESULT_OK && player_)
            lnplayer_set_external_subtitle(player_, "");
        QMetaObject::invokeMethod(this, [this, path, result]
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                subtitle_manager_.clear();
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕：%1").arg(QFileInfo(path).fileName()));
            }
            else
            {
                QString error;
                if (!subtitle_manager_.load_file(path, error))
                {
                    status_label_->setText(error);
                    return;
                }
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕（基础文本回退）：%1")
                        .arg(QFileInfo(path).fileName()));
            }

            pending_video_frame_valid_ = false;
            requestVideoFrame();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

QString MainWindow::subtitleAt(int64_t position_ms) const
{
    return subtitle_manager_.text_at(
        position_ms, subtitle_offset_ms_, subtitles_enabled_);
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
    if (mode_ != PlayerMode::Video || !frame_worker_ || !has_video_ || video_frame_pending_ ||
        video_seek_pending_ || video_end_of_stream_)
        return;

    video_frame_pending_ = true;
    QMetaObject::invokeMethod(frame_worker_, [this]
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
                if (video_surface_)
                {
                    video_surface_->set_hdr_metadata(
                        hdr_transfer, color_primaries, hdr_peak_luminance);
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
    }, Qt::QueuedConnection);
}

void MainWindow::presentPendingVideoFrame()
{
    if (!pending_video_frame_valid_ || !video_surface_)
        return;

    if (is_playing_)
    {
        int64_t clock_ms = current_position_ms_;
        if (video_clock_timer_.isValid())
            clock_ms = video_clock_anchor_position_ms_ +
                static_cast<int64_t>(video_clock_timer_.elapsed() * playback_rate_);
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
            QFont font(subtitle_font_family_);
            font.setPixelSize(subtitle_font_size_ > 0
                ? subtitle_font_size_
                : std::max(20, overlay.width() / 40));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QRect text_rect(
                overlay.width() / 20,
                overlay.height() * subtitle_vertical_position_ / 100,
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
            painter.setPen(QColor(subtitle_color_));
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
        const QImage embedded_subtitle_bitmap = pending_embedded_subtitle_bitmap_;
        const bool render_subtitles = subtitles_enabled_;
        const int ycbcr_matrix = pending_video_ycbcr_matrix_;
        const int ycbcr_full_range = pending_video_ycbcr_full_range_;
        pending_hardware_texture_ = nullptr;
        pending_video_frame_valid_ = false;

        QMetaObject::invokeMethod(frame_worker_,
            [this, owner = std::move(owner), texture, array_slice, format,
             width, height, generation, pts_ms, subtitle,
             embedded_subtitle_bitmap, render_subtitles,
             ycbcr_matrix, ycbcr_full_range, overlay = std::move(overlay)]
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
                    if (render_subtitles && !embedded_subtitle_bitmap.isNull())
                    {
                        QPainter bitmap_painter(&fallback.image);
                        bitmap_painter.drawImage(fallback.image.rect(), embedded_subtitle_bitmap);
                    }
                    if (!subtitle.isEmpty())
                    {
                        QPainter painter(&fallback.image);
                        painter.setRenderHint(QPainter::TextAntialiasing, true);
                        QFont font(subtitle_font_family_);
                        font.setPixelSize(subtitle_font_size_ > 0 ? subtitle_font_size_ : std::max(20, fallback.image.width() / 40));
                        font.setWeight(QFont::DemiBold);
                        painter.setFont(font);
                        const QRect text_rect(
                            fallback.image.width() / 20,
                            fallback.image.height() * subtitle_vertical_position_ / 100,
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
                        painter.setPen(QColor(subtitle_color_));
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
        QFont font(subtitle_font_family_);
        font.setPixelSize(subtitle_font_size_ > 0 ? subtitle_font_size_ : std::max(20, frame.width() / 40));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        const QRect text_rect(
            frame.width() / 20,
            frame.height() * subtitle_vertical_position_ / 100,
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
        painter.setPen(QColor(subtitle_color_));
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
        seek_slider_->setEnabled(false);
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
                    seek_slider_->setEnabled(duration_ms_ > 0);
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
                    seek_slider_->setEnabled(duration_ms_ > 0);
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
    showOsd(offset_ms < 0
        ? QStringLiteral("后退 %1 秒").arg(-offset_ms / 1000)
        : QStringLiteral("前进 %1 秒").arg(offset_ms / 1000));
    if (duration_ms_ <= 0 || video_seek_pending_)
        return;

    viewing_historical_frame_ = false;
    video_seek_pending_ = true;
    seek_slider_->setEnabled(false);
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
                seek_slider_->setEnabled(duration_ms_ > 0);
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

    QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
    const QString last_directory = settings.value(
        QStringLiteral("Video/Screenshot/LastDirectory"),
        QDir::homePath()).toString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存视频截图"),
        QDir(last_directory).filePath(QStringLiteral("screenshot.png")),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"));
    if (path.isEmpty())
        return;

    if (!screenshot.save(path))
    {
        statusBar()->showMessage(QStringLiteral("截图保存失败"));
        return;
    }

    settings.setValue(
        QStringLiteral("Video/Screenshot/LastDirectory"),
        QFileInfo(path).absolutePath());
    settings.sync();
    statusBar()->showMessage(QStringLiteral("截图已保存：%1").arg(path));
    showOsd(QStringLiteral("截图已保存"));
}

void MainWindow::setPlaybackRate(float rate)
{
    if (is_playing_ && video_clock_timer_.isValid())
    {
        video_clock_anchor_position_ms_ = video_clock_anchor_position_ms_ +
            static_cast<int64_t>(video_clock_timer_.elapsed() * playback_rate_);
        video_clock_timer_.restart();
    }
    else
    {
        video_clock_anchor_position_ms_ = current_position_ms_;
    }
    playback_rate_ = rate;
    showOsd(QStringLiteral("播放速度 %1x").arg(rate, 0, 'f', 2));
    QSettings settings(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"));
    settings.setValue(QStringLiteral("Video/PlaybackRate"), rate);
    settings.sync();

    QMetaObject::invokeMethod(command_worker_, [this, rate]
    {
        if (player_)
            lnplayer_set_playback_rate(player_, rate);
    }, Qt::QueuedConnection);
}

void MainWindow::stepVideoFrame(bool backwards)
{
    if (mode_ != PlayerMode::Video || !has_video_ || video_seek_pending_)
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
        video_surface_->set_hdr_metadata(
            entry.hdr_transfer, entry.color_primaries, entry.hdr_peak_luminance);
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
            QFont font(subtitle_font_family_);
            font.setPixelSize(subtitle_font_size_ > 0 ? subtitle_font_size_ : std::max(20, overlay.width() / 40));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            const QRect text_rect(
                overlay.width() / 20,
                overlay.height() * subtitle_vertical_position_ / 100,
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
            painter.setPen(QColor(subtitle_color_));
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
        QFont font(subtitle_font_family_);
        font.setPixelSize(subtitle_font_size_ > 0 ? subtitle_font_size_ : std::max(20, frame.width() / 40));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        const QRect text_rect(
            frame.width() / 20,
            frame.height() * subtitle_vertical_position_ / 100,
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
        painter.setPen(QColor(subtitle_color_));
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
            seek_slider_->setEnabled(duration_ms_ > 0);
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
    video_clock_anchor_position_ms_ = position_ms;
    if (is_playing_)
        video_clock_timer_.restart();
    else
        video_clock_timer_.invalidate();
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
