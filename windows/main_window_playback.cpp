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
QString application_data_directory()
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

QString format_position(int64_t position_ms)
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

QString settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}
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
        video_timer_->start(0);
    }

    postCommand([this, should_pause, resume_from_history, resume_position]
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
    });
}

void MainWindow::seekRelative(int64_t offset_ms)
{
    if (duration_ms_ <= 0 || video_seek_pending_)
        return;
    const QString message = offset_ms < 0
        ? QStringLiteral("后退 %1 秒").arg(-offset_ms / 1000)
        : QStringLiteral("前进 %1 秒").arg(offset_ms / 1000);
    seekToPosition(current_position_ms_ + offset_ms, message);
}

void MainWindow::seekToPosition(int64_t position_ms, const QString& message)
{
    if (video_seek_pending_)
        return;

    showOsd(message);
    viewing_historical_frame_ = false;
    video_seek_pending_ = true;
    seek_slider_->setEnabled(false);
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    video_end_of_stream_ = false;
    video_timer_->start(0);
    const int64_t target = duration_ms_ > 0
        ? std::clamp<int64_t>(position_ms, 0, duration_ms_)
        : std::max<int64_t>(0, position_ms);
    postCommand([this, target]
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
    });
}

void MainWindow::setPlaybackRate(float rate)
{
    if (is_playing_ && video_clock_timer_.isValid())
    {
        video_clock_anchor_position_ms_ = video_clock_anchor_position_ms_ +
            static_cast<int64_t>(std::llround(
                video_clock_timer_.nsecsElapsed() / 1000000.0 * playback_rate_));
        video_clock_timer_.restart();
    }
    else
    {
        video_clock_anchor_position_ms_ = current_position_ms_;
    }
    playback_rate_ = rate;
    if (pending_video_frame_valid_)
        scheduleVideoPresentation();
    showOsd(QStringLiteral("播放速度 %1x").arg(rate, 0, 'f', 2));
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/PlaybackRate"), rate);
    settings.sync();

    postCommand([this, rate]
    {
        if (player_)
            lnplayer_set_playback_rate(player_, rate);
    });
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
            video_info_ = {};
            postCommand([this]
            {
                lnplayer_video_info_t info{};
                info.struct_size = sizeof(info);
                const lnplayer_result_t info_result = player_
                    ? lnplayer_get_video_info(player_, &info)
                    : LNPLAYER_RESULT_INVALID_HANDLE;
                if (info_result == LNPLAYER_RESULT_OK)
                {
                    QMetaObject::invokeMethod(this, [this, info]
                    {
                        video_info_ = info;
                    }, Qt::QueuedConnection);
                }
            });
            status_label_->setText(QStringLiteral("媒体已打开"));
            statusBar()->showMessage(QStringLiteral("就绪"));
            seek_slider_->setEnabled(duration_ms > 0);
            if (mode_ == PlayerMode::Video && remember_position_ &&
                playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const QString key = playbackPositionSettingKey(
                    playlist_[playlist_index_].toLocalFile());
                const int64_t restore_position = settings.value(key, 0).toLongLong();
                if (restore_position > 5000 &&
                    (duration_ms <= 0 || restore_position < duration_ms - 5000))
                {
                    video_seek_pending_ = true;
                    seek_slider_->setEnabled(false);
                    postCommand([this, restore_position]
                    {
                        uint64_t operation_id = 0;
                        const lnplayer_result_t seek_result = player_
                            ? lnplayer_seek(player_, restore_position, &operation_id)
                            : LNPLAYER_RESULT_INVALID_HANDLE;
                        if (seek_result != LNPLAYER_RESULT_OK)
                        {
                            QMetaObject::invokeMethod(this, [this]
                            {
                                video_seek_pending_ = false;
                                statusBar()->showMessage(QStringLiteral("恢复播放位置失败"));
                            }, Qt::QueuedConnection);
                        }
                    });
                }
            }
            if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const int saved_audio_track = settings.value(
                    mediaPreferenceKey(QStringLiteral("AudioTrack"), current_media_path_), -1).toInt();
                if (saved_audio_track >= 0)
                    selectAudioTrack(saved_audio_track);
            }
            if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const QString external_subtitle_key = mediaPreferenceKey(
                    QStringLiteral("ExternalSubtitle"), current_media_path_);
                const QString external_subtitle_path = settings.value(external_subtitle_key).toString();
                if (!external_subtitle_path.isEmpty() && QFileInfo::exists(external_subtitle_path))
                {
                    loadExternalSubtitlePath(external_subtitle_path, false);
                }
                else
                {
                    if (!external_subtitle_path.isEmpty())
                    {
                        settings.remove(external_subtitle_key);
                        settings.sync();
                    }
                    const QString subtitle_track_key = mediaPreferenceKey(
                        QStringLiteral("SubtitleTrack"), current_media_path_);
                    if (settings.contains(subtitle_track_key))
                    {
                        const int saved_subtitle_track = settings.value(subtitle_track_key).toInt();
                        selectSubtitleTrack(saved_subtitle_track);
                    }
                    else if (auto_match_subtitle_)
                    {
                        const QFileInfo media_info(playlist_[playlist_index_].toLocalFile());
                        const QString base_name = media_info.completeBaseName();
                        const QStringList subtitle_extensions = {
                            QStringLiteral(".srt"), QStringLiteral(".ass"), QStringLiteral(".ssa")
                        };
                        for (const QString& extension : subtitle_extensions)
                        {
                            const QString candidate = QDir(media_info.absolutePath())
                                .filePath(base_name + extension);
                            if (QFileInfo::exists(candidate))
                            {
                                loadExternalSubtitlePath(candidate, false);
                                break;
                            }
                        }
                    }
                }
            }
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
            if (empty_state_label_)
            {
                empty_state_label_->setText(message.isEmpty()
                    ? QStringLiteral("无法打开此视频文件")
                    : message);
                empty_state_label_->adjustSize();
                empty_state_label_->move(
                    (video_surface_->width() - empty_state_label_->width()) / 2,
                    (video_surface_->height() - empty_state_label_->height()) / 2);
                empty_state_label_->show();
                empty_state_label_->raise();
            }
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
    {
        if (playlist_open_pending_ || video_seek_pending_)
            break;
        if (repeat_mode_ == 2 && playlist_index_ >= 0)
        {
            openPlaylistIndex(playlist_index_);
            break;
        }
        if (!auto_play_next_ && repeat_mode_ != 1)
        {
            video_timer_->stop();
            status_label_->setText(QStringLiteral("播放结束"));
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
    }

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
    if (snapshot_pending_)
        return;
    snapshot_pending_ = true;

    postBackground([this]
    {
        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        lnplayer_t* player = player_.load(std::memory_order_acquire);
        const lnplayer_result_t result = player
            ? lnplayer_get_snapshot(player, &snapshot)
            : LNPLAYER_RESULT_INVALID_HANDLE;

        QMetaObject::invokeMethod(this, [this, result, snapshot]
        {
            snapshot_pending_ = false;
            if (result != LNPLAYER_RESULT_OK)
                return;

            media_generation_ = std::max(media_generation_, snapshot.generation);
            has_audio_ = (snapshot.reserved & LNPLAYER_MEDIA_HAS_AUDIO) != 0;
            has_video_ = (snapshot.reserved & LNPLAYER_MEDIA_HAS_VIDEO) != 0;
            updatePlaybackState(snapshot.state, snapshot.position_ms, snapshot.duration_ms);

            if (mode_ == PlayerMode::Video && !has_video_ &&
                !playlist_open_pending_ && !current_media_path_.isEmpty() && empty_state_label_)
            {
                empty_state_label_->setText(QStringLiteral("当前媒体没有可播放的视频流"));
                empty_state_label_->adjustSize();
                empty_state_label_->move(
                    (video_surface_->width() - empty_state_label_->width()) / 2,
                    (video_surface_->height() - empty_state_label_->height()) / 2);
                empty_state_label_->show();
                empty_state_label_->raise();
            }
            if (has_video_ && !video_first_frame_received_ &&
                !pending_video_frame_valid_ && !video_frame_pending_)
                requestVideoFrame();
        }, Qt::QueuedConnection);
    });
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

    if (mode_ == PlayerMode::Video && state == LNPLAYER_STATE_PLAYING &&
        loop_a_ms_ >= 0 && loop_b_ms_ > loop_a_ms_ &&
        position_ms >= loop_b_ms_ && !video_seek_pending_)
    {
        video_seek_pending_ = true;
        const int64_t loop_target = loop_a_ms_;
        postCommand([this, loop_target]
        {
            uint64_t operation_id = 0;
            const lnplayer_result_t result = player_
                ? lnplayer_seek(player_, loop_target, &operation_id)
                : LNPLAYER_RESULT_INVALID_HANDLE;
            if (result != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    loop_a_ms_ = -1;
                    loop_b_ms_ = -1;
                    statusBar()->showMessage(QStringLiteral("A-B 循环定位失败，已关闭区间循环"));
                }, Qt::QueuedConnection);
            }
        });
    }

    const bool was_playing = is_playing_;
    is_playing_ = state == LNPLAYER_STATE_PLAYING;
    play_button_->setIcon(PlayerIcons::make(
        is_playing_ ? PlayerIcons::Name::Pause : PlayerIcons::Name::Play));
    play_button_->setToolTip(is_playing_ ? QStringLiteral("暂停") : QStringLiteral("播放"));
    play_button_->setAccessibleName(play_button_->toolTip());
    video_clock_anchor_position_ms_ = position_ms;
    if (is_playing_)
        video_clock_timer_.restart();
    else
        video_clock_timer_.invalidate();
    if (is_playing_ && pending_video_frame_valid_)
        scheduleVideoPresentation();
    if (mode_ == PlayerMode::Video && has_video_)
    {
        if (is_playing_)
        {
            if (!was_playing)
                video_end_of_stream_ = false;
            if (!video_timer_->isActive())
                video_timer_->start(0);
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
