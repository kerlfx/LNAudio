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
QString settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}
}

void MainWindow::loadExternalSubtitle()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载外挂字幕"),
        QString(),
        QStringLiteral("字幕文件 (*.srt *.ass *.ssa);;SubRip (*.srt);;ASS/SSA (*.ass *.ssa);;所有文件 (*)"));
    if (!path.isEmpty())
        loadExternalSubtitlePath(path);
}

void MainWindow::loadExternalSubtitlePath(const QString& path, bool remember)
{
    const QByteArray path_utf8 = path.toUtf8();
    const QString media_path = current_media_path_;
    postCommand([this, path, path_utf8, media_path, remember]
    {
        const lnplayer_result_t result = player_
            ? lnplayer_set_external_subtitle(player_, path_utf8.constData())
            : LNPLAYER_RESULT_INVALID_HANDLE;
        if (result != LNPLAYER_RESULT_OK && player_)
            lnplayer_set_external_subtitle(player_, "");

        if (result == LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this, path, media_path, remember]
            {
                if (current_media_path_ != media_path)
                    return;
                subtitle_manager_.clear();
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕：%1").arg(QFileInfo(path).fileName()));
                if (remember && !media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path), path);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path));
                    settings.sync();
                }
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }, Qt::QueuedConnection);
            return;
        }

        postBackground([this, path, media_path, remember]
        {
            auto fallback_manager = std::make_shared<SubtitleManager>();
            QString error;
            const bool loaded = fallback_manager->load_file(path, error);
            QMetaObject::invokeMethod(this,
                [this, path, media_path, remember, fallback_manager, error, loaded]
            {
                if (current_media_path_ != media_path)
                    return;
                if (!loaded)
                {
                    status_label_->setText(error);
                    return;
                }

                subtitle_manager_ = std::move(*fallback_manager);
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕（基础文本回退）：%1")
                        .arg(QFileInfo(path).fileName()));
                if (remember && !media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path), path);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path));
                    settings.sync();
                }
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }, Qt::QueuedConnection);
        });
    });
}

void MainWindow::selectSubtitleTrack(int stream_index)
{
    const QString media_path = current_media_path_;
    postCommand([this, stream_index, media_path]
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

        const lnplayer_result_t result = lnplayer_select_subtitle_track(player_, stream_index);
        const lnplayer_result_t resume_result = resume_playback
            ? lnplayer_play(player_)
            : LNPLAYER_RESULT_OK;

        QMetaObject::invokeMethod(this,
            [this, result, resume_playback, resume_result, stream_index, media_path]
        {
            is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
            if (result == LNPLAYER_RESULT_OK)
            {
                if (!media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path),
                        stream_index);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path));
                    settings.sync();
                }
                pending_embedded_subtitle_.clear();
                pending_embedded_subtitle_bitmap_ = {};
                video_frame_history_.clear();
                video_frame_history_index_ = -1;
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                showOsd(resume_result == LNPLAYER_RESULT_OK
                    ? QStringLiteral("内嵌字幕已切换")
                    : QStringLiteral("内嵌字幕已切换，但恢复播放失败"));
            }
            else
            {
                statusBar()->showMessage(QStringLiteral("内嵌字幕切换失败"));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::selectAudioTrack(int stream_index)
{
    const QString media_path = current_media_path_;
    postCommand([this, stream_index, media_path]
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

        const lnplayer_result_t result = lnplayer_select_audio_track(player_, stream_index);
        const lnplayer_result_t resume_result = resume_playback
            ? lnplayer_play(player_)
            : LNPLAYER_RESULT_OK;

        QMetaObject::invokeMethod(this,
            [this, result, resume_playback, resume_result, stream_index, media_path]
        {
            is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
            if (result == LNPLAYER_RESULT_OK)
            {
                if (!media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("AudioTrack"), media_path),
                        stream_index);
                    settings.sync();
                }
                video_frame_history_.clear();
                video_frame_history_index_ = -1;
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                showOsd(resume_result == LNPLAYER_RESULT_OK
                    ? QStringLiteral("音轨已切换")
                    : QStringLiteral("音轨已切换，但恢复播放失败"));
            }
            else
            {
                statusBar()->showMessage(QStringLiteral("音轨切换失败"));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setSubtitleOffset(int64_t offset_ms)
{
    subtitle_offset_ms_ = std::clamp<int64_t>(offset_ms, -60000, 60000);
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/Subtitle/OffsetMs"), subtitle_offset_ms_);
    settings.sync();

    const int64_t applied_offset = subtitle_offset_ms_;
    postCommand([this, applied_offset]
    {
        if (player_)
            lnplayer_set_subtitle_offset(player_, applied_offset);
        QMetaObject::invokeMethod(this, [this, applied_offset]
        {
            pending_video_frame_valid_ = false;
            requestVideoFrame();
            showOsd(QStringLiteral("字幕偏移：%1 ms").arg(applied_offset));
        }, Qt::QueuedConnection);
    });
}

QString MainWindow::subtitleAt(int64_t position_ms) const
{
    return subtitle_manager_.text_at(
        position_ms, subtitle_offset_ms_, subtitles_enabled_);
}
