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

 settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}

class PlaylistItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem item_option(option);
        initStyleOption(&item_option, index);
        const QString title = item_option.text;
        item_option.text.clear();
        const QWidget* widget = item_option.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item_option, painter, widget);

        const QRect text_rect = style->subElementRect(
            QStyle::SE_ItemViewItemText, &item_option, widget);
        const QColor text_color = item_option.state & QStyle::State_Selected
            ? item_option.palette.color(QPalette::HighlightedText)
            : item_option.palette.color(QPalette::Text);
        painter->save();
        painter->setPen(text_color);
        const QFontMetrics metrics(item_option.font);
        const QString duration = index.data(Qt::UserRole + 1).toString();
        const QString resolution = index.data(Qt::UserRole + 2).toString();
        const QString metadata = QStringLiteral("%1  %2").arg(duration, resolution);
        QFont metadata_font = item_option.font;
        metadata_font.setPointSize(std::max(8, metadata_font.pointSize() - 1));
        const QFontMetrics metadata_metrics(metadata_font);
        const int metadata_width = std::min(
            text_rect.width() / 2,
            metadata_metrics.horizontalAdvance(metadata) + 8);
        const QRect title_rect(
            text_rect.left(), text_rect.top(),
            text_rect.width() - metadata_width, text_rect.height());
        const QRect metadata_rect(
            title_rect.right() + 4, text_rect.top(),
            metadata_width, text_rect.height());
        painter->drawText(title_rect, Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(title, Qt::ElideRight, title_rect.width()));
        painter->setFont(metadata_font);
        painter->setPen(item_option.palette.color(QPalette::Mid));
        painter->drawText(metadata_rect, Qt::AlignVCenter | Qt::AlignRight,
            metadata_metrics.elidedText(metadata, Qt::ElideLeft, metadata_rect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 32));
        return size;
    }
};

 create_playlist_header(QWidget* parent)
{
    auto* header = new QWidget(parent);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(6, 2, 6, 2);
    auto* title = new QLabel(QStringLiteral("文件名"), header);
    auto* metadata = new QLabel(QStringLiteral("时长  ·  分辨率"), header);
    metadata->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(title, 1);
    layout->addWidget(metadata);
    header->setStyleSheet(QStringLiteral("QWidget { color: palette(mid); font-size: 11px; }"));
    return header;
}

 expand_media_urls(
    const QList<QUrl>& urls,
    const std::shared_ptr<UiTaskTracker>& tracker)
{
    const QStringList video_filters = {
        QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
        QStringLiteral("*.avi"), QStringLiteral("*.webm"), QStringLiteral("*.wmv"),
        QStringLiteral("*.flv"), QStringLiteral("*.mpeg"), QStringLiteral("*.mpg"),
        QStringLiteral("*.m4v"), QStringLiteral("*.ts"), QStringLiteral("*.mts"),
        QStringLiteral("*.m2ts"), QStringLiteral("*.3gp"), QStringLiteral("*.3g2"),
        QStringLiteral("*.vob"), QStringLiteral("*.ogv"), QStringLiteral("*.mxf"),
        QStringLiteral("*.asf"), QStringLiteral("*.f4v"), QStringLiteral("*.divx"),
        QStringLiteral("*.mpe"), QStringLiteral("*.mpv")
    };
    QStringList paths;
    QSet<QString> unique_paths;
    for (const QUrl& url : urls)
    {
        if (!tracker->accepting())
            return paths;
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        const QFileInfo file_info(path);
        if (file_info.isDir())
        {
            QDirIterator iterator(
                file_info.absoluteFilePath(), video_filters,
                QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (iterator.hasNext() && tracker->accepting())
            {
                const QString candidate = QFileInfo(iterator.next()).absoluteFilePath();
                const QString key = candidate.toCaseFolded();
                if (!unique_paths.contains(key))
                {
                    unique_paths.insert(key);
                    paths.append(candidate);
                }
            }
        }
        else if (file_info.isFile())
        {
            const QString candidate = file_info.absoluteFilePath();
            const QString key = candidate.toCaseFolded();
            if (!unique_paths.contains(key))
            {
                unique_paths.insert(key);
                paths.append(candidate);
            }
        }
    }
    if (!tracker->accepting())
        return paths;
    std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right)
    {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return paths;
}
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
    if (urls.isEmpty())
        return;

    savePlaybackPosition();
    const uint64_t operation_generation = ++playlist_operation_generation_;
    status_label_->setText(QStringLiteral("正在扫描媒体文件..."));
    const auto tracker = task_tracker_;
    postLibrary([this, tracker, urls, operation_generation]
    {
        const QStringList paths = expand_media_urls(urls, tracker);
        QMetaObject::invokeMethod(this, [this, paths, operation_generation]
        {
            if (operation_generation != playlist_operation_generation_)
                return;
            QList<QUrl> next_playlist;
            for (const QString& path : paths)
                next_playlist.append(QUrl::fromLocalFile(path));
            if (next_playlist.isEmpty())
            {
                status_label_->setText(QStringLiteral("没有找到支持的媒体文件"));
                return;
            }

            for (const QString& queued_path : playlist_probe_queue_)
                playlist_probe_pending_.remove(queued_path);
            playlist_probe_queue_.clear();
            playlist_ = std::move(next_playlist);
            playlist_index_ = 0;
            resetShuffleOrder();
            rebuildPlaylistWidget();
            saveVideoPlaylist();
            openPlaylistIndex(0);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::appendPlaylistFiles(const QList<QUrl>& urls)
{
    if (urls.isEmpty())
        return;

    const uint64_t operation_generation = playlist_operation_generation_;
    const auto tracker = task_tracker_;
    postLibrary([this, tracker, urls, operation_generation]
    {
        const QStringList paths = expand_media_urls(urls, tracker);
        QMetaObject::invokeMethod(this, [this, paths, operation_generation]
        {
            if (operation_generation != playlist_operation_generation_)
                return;
            for (const QString& path : paths)
            {
                playlist_.append(QUrl::fromLocalFile(path));
                schedulePlaylistMetadataProbe(path);
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
        }, Qt::QueuedConnection);
    });
}

void MainWindow::rebuildPlaylistWidget()
{
    if (!playlist_widget_)
        return;
    playlist_item_index_.clear();
    playlist_widget_->setUpdatesEnabled(false);
    playlist_widget_->clear();
    for (const QUrl& url : playlist_)
    {
        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        const QFileInfo file_info(path);
        auto* item = new QListWidgetItem(file_info.fileName(), playlist_widget_);
        item->setData(Qt::UserRole, path);
        playlist_item_index_.insert(path, item);
        updatePlaylistItemMetadata(path);
        if (!playlist_video_metadata_.contains(path))
            schedulePlaylistMetadataProbe(path);
    }
    playing_playlist_item_ = nullptr;
    if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
    {
        playlist_widget_->setCurrentRow(playlist_index_);
        playing_playlist_item_ = playlist_widget_->item(playlist_index_);
    }
    playlist_widget_->setUpdatesEnabled(true);
    playlist_widget_->viewport()->update();
}

void MainWindow::schedulePlaylistMetadataProbe(const QString& path)
{
    if (path.isEmpty() || playlist_video_metadata_.contains(path) ||
        playlist_probe_pending_.contains(path))
        return;

    playlist_probe_pending_.insert(path);
    playlist_probe_queue_.push_back(path);
    startNextPlaylistMetadataProbe();
}

void MainWindow::startNextPlaylistMetadataProbe()
{
    if (playlist_probe_running_ || playlist_probe_queue_.empty())
        return;

    const QString path = std::move(playlist_probe_queue_.front());
    playlist_probe_queue_.pop_front();
    const QByteArray path_utf8 = path.toUtf8();
    playlist_probe_running_ = true;
    postLibrary([this, path, path_utf8]
    {
        lnplayer_video_info_t info{};
        info.struct_size = sizeof(info);
        int64_t duration_ms = -1;
        const lnplayer_result_t result = lnplayer_probe_media(
            path_utf8.constData(), &duration_ms, &info);
        QMetaObject::invokeMethod(this, [this, path, info, duration_ms, result]
        {
            playlist_probe_pending_.remove(path);
            if (result == LNPLAYER_RESULT_OK)
            {
                playlist_video_metadata_.insert(path, info);
                playlist_durations_.insert(path, duration_ms);
            }
            updatePlaylistItemMetadata(path);
            playlist_probe_running_ = false;
            startNextPlaylistMetadataProbe();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updatePlaylistItemMetadata(const QString& path)
{
    if (!playlist_widget_)
        return;

    const auto items = playlist_item_index_.values(path);
    if (items.isEmpty())
        return;

    const QFileInfo file_info(path);
    const auto metadata = playlist_video_metadata_.constFind(path);
    QString duration_text = QStringLiteral("--:--");
    QString resolution_text = QStringLiteral("未知");
    QString details = QStringLiteral("%1 bytes")
        .arg(file_info.exists() ? file_info.size() : 0);
    if (metadata != playlist_video_metadata_.cend())
    {
        const lnplayer_video_info_t& info = metadata.value();
        const int64_t duration = playlist_durations_.value(path, -1);
        if (duration >= 0)
            duration_text = format_position(duration);
        if (info.width > 0 && info.height > 0)
            resolution_text = QStringLiteral("%1×%2").arg(info.width).arg(info.height);
        details += QStringLiteral("\n容器：%1\n编码：%2\n像素格式：%3\n码率：%4")
            .arg(QString::fromUtf8(info.container_long_name))
            .arg(QString::fromUtf8(info.codec_name))
            .arg(QString::fromUtf8(info.pixel_format))
            .arg(info.bit_rate > 0
                ? QString::number(info.bit_rate / 1000) + QStringLiteral(" kbps")
                : QStringLiteral("未知"));
    }

    for (QListWidgetItem* item : items)
    {
        item->setText(file_info.fileName());
        item->setData(Qt::UserRole + 1, duration_text);
        item->setData(Qt::UserRole + 2, resolution_text);
        item->setToolTip(QStringLiteral("%1\n%2").arg(file_info.absoluteFilePath(), details));
    }
    playlist_widget_->viewport()->update();
}

void MainWindow::loadVideoPlaylist()
{
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    const QStringList files = settings.value(QStringLiteral("VideoPlaylist/Files")).toStringList();
    playlist_.clear();
    for (const QString& path : files)
    {
        if (QFileInfo::exists(path))
        {
            playlist_.append(QUrl::fromLocalFile(path));
            schedulePlaylistMetadataProbe(QFileInfo(path).absoluteFilePath());
        }
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
        shuffle_button_->setIcon(PlayerIcons::make(PlayerIcons::Name::Shuffle,
            shuffle_enabled_ ? QColor(QStringLiteral("#d7dde7"))
                : QColor(QStringLiteral("#8f99a8"))));
        shuffle_button_->setToolTip(shuffle_enabled_
            ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关"));
        shuffle_button_->setAccessibleName(shuffle_button_->toolTip());
    }
    if (repeat_button_)
    {
        repeat_button_->setIcon(PlayerIcons::make(
            repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
            repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                : QColor(QStringLiteral("#d7dde7"))));
        repeat_button_->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
        repeat_button_->setAccessibleName(repeat_button_->toolTip());
    }
    resetShuffleOrder();
    rebuildPlaylistWidget();
    if (playlist_index_ >= 0)
        openPlaylistIndex(playlist_index_);
}

QString MainWindow::playbackPositionSettingKey(const QString& path) const
{
    const QByteArray normalized_path = QFileInfo(path).absoluteFilePath().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(
        normalized_path, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("Video/PlaybackPositions/") + QString::fromLatin1(digest);
}

QString MainWindow::mediaPreferenceKey(const QString& category, const QString& path) const
{
    const QByteArray normalized_path = QFileInfo(path).absoluteFilePath().toCaseFolded().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(
        normalized_path, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("Video/MediaPreferences/") + category +
        QStringLiteral("/") + QString::fromLatin1(digest);
}

void MainWindow::savePlaybackPosition() const
{
    if (!remember_position_ || mode_ != PlayerMode::Video || current_media_path_.isEmpty())
        return;

    const QString path = current_media_path_;
    if (path.isEmpty())
        return;

    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(playbackPositionSettingKey(path), current_position_ms_);
    settings.sync();
}

void MainWindow::saveVideoPlaylist() const
{
    if (mode_ != PlayerMode::Video)
        return;

    QSettings settings(settings_file_path(), QSettings::IniFormat);
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
    ++playlist_operation_generation_;
    savePlaybackPosition();
    for (const QString& queued_path : playlist_probe_queue_)
        playlist_probe_pending_.remove(queued_path);
    playlist_probe_queue_.clear();
    playlist_.clear();
    playlist_index_ = -1;
    current_media_path_.clear();
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
    video_width_ = 0;
    video_height_ = 0;
    if (seek_slider_)
    {
        seek_slider_->setEnabled(false);
        seek_slider_->setValue(0);
    }
    video_first_frame_received_ = false;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    video_width_ = 0;
    video_height_ = 0;
    pending_video_frame_valid_ = false;
    pending_hardware_frame_owner_.reset();
    pending_hardware_texture_ = nullptr;
    video_timer_->stop();

    playlist_item_index_.clear();
    if (playlist_widget_)
        playlist_widget_->clear();
    if (video_surface_)
        video_surface_->set_frame(QImage());
    if (empty_state_label_)
    {
        empty_state_label_->setText(
            QStringLiteral("拖放视频文件到此处，或从“文件”菜单打开视频"));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        empty_state_label_->raise();
    }
    media_title_label_->setText(QStringLiteral("未打开媒体"));
    position_label_->setText(QStringLiteral("00:00 / --:--"));
    status_label_->setText(QStringLiteral("播放列表已清空"));
    statusBar()->showMessage(QStringLiteral("播放列表已清空"));

    postCommand([this]
    {
        if (player_)
            lnplayer_close(player_);
    });
    saveVideoPlaylist();
}

void MainWindow::showPlaylistWindow()
{
    if (mode_ != PlayerMode::Video || !playlist_widget_)
        return;

    if (playlist_dialog_)
    {
        playlist_dialog_->show();
        playlist_dialog_->raise();
        playlist_dialog_->activateWindow();
        return;
    }

    auto* dialog = new QDialog(this);
    playlist_dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("LNPlayer 播放列表"));
    dialog->resize(460, 560);

    auto* root = new QVBoxLayout(dialog);
    auto* view = new QListView(dialog);
    view->setItemDelegate(new PlaylistItemDelegate(view));
    view->setModel(playlist_widget_->model());
    root->addWidget(create_playlist_header(dialog));
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setDragDropMode(QAbstractItemView::InternalMove);
    view->setDropIndicatorShown(true);
    view->setDefaultDropAction(Qt::MoveAction);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(view, 1);

    auto* actions = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("添加文件"), dialog);
    auto* remove = new QPushButton(QStringLiteral("移除选中项"), dialog);
    auto* clear = new QPushButton(QStringLiteral("清空队列"), dialog);
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addWidget(clear);
    root->addLayout(actions);

    connect(view, &QAbstractItemView::doubleClicked, this,
        [this](const QModelIndex& index)
        {
            if (index.isValid())
                openPlaylistIndex(index.row());
        });
    connect(add, &QPushButton::clicked, dialog, [this]
    {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("添加到播放队列"), QString(),
            QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));
        QList<QUrl> urls;
        for (const QString& file : files)
            urls.append(QUrl::fromLocalFile(file));
        appendPlaylistFiles(urls);
    });
    connect(remove, &QPushButton::clicked, dialog, [this, view]
    {
        playlist_widget_->clearSelection();
        for (const QModelIndex& index : view->selectionModel()->selectedRows())
        {
            if (QListWidgetItem* item = playlist_widget_->item(index.row()))
                item->setSelected(true);
        }
        removeSelectedPlaylistItem();
    });
    connect(clear, &QPushButton::clicked, dialog, [this]
    {
        clearPlaylist();
    });
    connect(dialog, &QObject::destroyed, this, [this]
    {
        playlist_dialog_ = nullptr;
    });

    dialog->show();
}

void MainWindow::removeSelectedPlaylistItem()
{
    savePlaybackPosition();
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
    {
        repeat_button_->setIcon(PlayerIcons::make(
            repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
            repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                : QColor(QStringLiteral("#d7dde7"))));
        repeat_button_->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
        repeat_button_->setAccessibleName(repeat_button_->toolTip());
    }
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

    savePlaybackPosition();
    playlist_index_ = index;
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        QStringList recent_files = settings.value(
            QStringLiteral("Video/RecentFiles")).toStringList();
        const QString recent_path = QFileInfo(playlist_[index].toLocalFile()).absoluteFilePath();
        recent_files.removeAll(recent_path);
        recent_files.prepend(recent_path);
        while (recent_files.size() > 12)
            recent_files.removeLast();
        settings.setValue(QStringLiteral("Video/RecentFiles"), recent_files);
        settings.sync();
    }
    saveVideoPlaylist();
    if (playlist_widget_)
    {
        playlist_widget_->setCurrentRow(index);
        playing_playlist_item_ = playlist_widget_->item(index);
    }

    const QString source = playlist_[index].toLocalFile();
    if (source.isEmpty())
        return;
    current_media_path_ = QFileInfo(source).absoluteFilePath();
    has_video_ = false;
    has_audio_ = false;
    video_width_ = 0;
    video_height_ = 0;
    is_playing_ = false;
    duration_ms_ = -1;
    current_position_ms_ = 0;
    video_info_ = {};
    if (seek_slider_)
    {
        seek_slider_->setEnabled(false);
        seek_slider_->setValue(0);
    }

    subtitle_manager_.clear();
    loop_a_ms_ = -1;
    loop_b_ms_ = -1;
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
    if (empty_state_label_)
    {
        empty_state_label_->setText(QStringLiteral("正在打开视频..."));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        empty_state_label_->raise();
    }
    status_label_->setText(QStringLiteral("正在打开：%1").arg(source));
    statusBar()->showMessage(QStringLiteral("正在打开媒体"));
    playlist_open_pending_ = true;

    const QByteArray source_utf8 = source.toUtf8();
    postCommand([this, source_utf8]
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
    });
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
