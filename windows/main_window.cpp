#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
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
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QDropEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
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
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

class SeekSlider final : public QSlider
{
public:
    explicit SeekSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && !sliderHandleRect().contains(event->position().toPoint()))
        {
            groove_dragging_ = true;
            setSliderDown(true);
            setValueFromPosition(event->position().toPoint().x());
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (groove_dragging_ && (event->buttons() & Qt::LeftButton))
        {
            setValueFromPosition(event->position().toPoint().x());
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (groove_dragging_ && event->button() == Qt::LeftButton)
        {
            setValueFromPosition(event->position().toPoint().x());
            groove_dragging_ = false;
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void setValueFromPosition(int x)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect handle = style()->subControlRect(
            QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const int span = std::max(1, width() - handle.width());
        const int position = std::clamp(x - handle.width() / 2, 0, span);
        setValue(QStyle::sliderValueFromPosition(
            minimum(), maximum(), position, span, option.upsideDown));
    }

    bool groove_dragging_ = false;
};

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

QString settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}

void initialize_settings_file()
{
    const QString target_path = settings_file_path();
    const QString previous_ini = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("settings.ini"));
    if (!QFile::exists(target_path) && QFile::exists(previous_ini))
        QFile::copy(previous_ini, target_path);

    QSettings settings(target_path, QSettings::IniFormat);
    const QString migration_key = QStringLiteral("Config/LegacySettingsMigrationCompleted");
    if (settings.value(migration_key, false).toBool())
        return;

#ifdef Q_OS_WIN
    QSettings legacy(QStringLiteral("LNPlayer"), QStringLiteral("LNPlayer"),
        QSettings::NativeFormat);
    for (const QString& key : legacy.allKeys())
    {
        if (!settings.contains(key))
            settings.setValue(key, legacy.value(key));
    }
#endif
    settings.setValue(migration_key, true);
    settings.sync();
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


void draw_subtitle_text(
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

void draw_subtitle_bitmap(QImage& image, const QImage& bitmap, bool fit_aspect, float aspect)
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
        const int gap = 8;
        const int title_width = text_rect.width() * 55 / 100;
        const int duration_width = text_rect.width() * 20 / 100;
        const int resolution_width = text_rect.width() - title_width - duration_width - gap * 2;
        const QColor text_color = item_option.state & QStyle::State_Selected
            ? item_option.palette.color(QPalette::HighlightedText)
            : item_option.palette.color(QPalette::Text);
        painter->save();
        painter->setPen(text_color);
        const QFontMetrics metrics(item_option.font);
        painter->drawText(QRect(text_rect.left(), text_rect.top(), title_width,
            text_rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(title, Qt::ElideRight, title_width));
        const QString duration = index.data(Qt::UserRole + 1).toString();
        const QString resolution = index.data(Qt::UserRole + 2).toString();
        painter->drawText(QRect(text_rect.left() + title_width + gap, text_rect.top(),
            duration_width, text_rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(duration, Qt::ElideRight, duration_width));
        painter->drawText(QRect(text_rect.left() + title_width + duration_width + gap * 2,
            text_rect.top(), resolution_width, text_rect.height()),
            Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(resolution, Qt::ElideRight, resolution_width));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 34));
        return size;
    }
};

QWidget* create_playlist_header(QWidget* parent)
{
    auto* header = new QWidget(parent);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("文件名"), header);
    auto* duration = new QLabel(QStringLiteral("时长"), header);
    auto* resolution = new QLabel(QStringLiteral("分辨率"), header);
    layout->addWidget(title, 55);
    layout->addWidget(duration, 20);
    layout->addWidget(resolution, 25);
    header->setStyleSheet(QStringLiteral("QWidget { color: palette(mid); font-size: 11px; }"));
    return header;
}

QStringList expand_media_urls(const QList<QUrl>& urls)
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
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        const QFileInfo file_info(path);
        if (file_info.isDir())
        {
            QDirIterator iterator(
                file_info.absoluteFilePath(), video_filters,
                QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (iterator.hasNext())
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
    std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right)
    {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return paths;
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
    , seek_slider_(new SeekSlider(this))
    , volume_slider_(new QSlider(Qt::Horizontal, this))
    , play_button_(new QPushButton(QStringLiteral("播放"), this))
    , pause_button_(new QPushButton(QStringLiteral("暂停"), this))
    , stop_button_(new QPushButton(QStringLiteral("停止"), this))
    , ui_io_work_guard_(asio::make_work_guard(ui_io_context_))
    , ui_thread_pool_(3)
    , command_executor_(asio::make_strand(ui_thread_pool_))
    , frame_executor_(asio::make_strand(ui_thread_pool_))
    , library_executor_(asio::make_strand(ui_thread_pool_))
    , snapshot_timer_(new QTimer(this))
    , video_timer_(new QTimer(this))
    , device_timer_(new QTimer(this))
    , fullscreen_hide_timer_(new QTimer(this))
    , osd_timer_(new QTimer(this))
    , volume_settings_timer_(new QTimer(this))
{
    initialize_settings_file();
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        playback_rate_ = std::clamp(
            settings.value(QStringLiteral("Video/PlaybackRate"), 1.0).toFloat(),
            0.25f, 4.0f);
        playlist_panel_visible_ = settings.value(
            QStringLiteral("Video/UI/PlaylistVisible"), true).toBool();
        fullscreen_hide_delay_ms_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/FullscreenHideDelayMs"), 2000).toInt(),
            0, 10000);
        osd_duration_ms_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/OSDDurationMs"), 1500).toInt(),
            500, 5000);
        osd_position_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/OSDPosition"), 1).toInt(), 0, 2);
        screenshot_directory_ = settings.value(
            QStringLiteral("Video/Screenshot/Directory"),
            QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))).toString();
        screenshot_format_ = settings.value(
            QStringLiteral("Video/Screenshot/Format"), QStringLiteral("png")).toString();
        if (screenshot_format_ != QStringLiteral("jpg") &&
            screenshot_format_ != QStringLiteral("bmp"))
            screenshot_format_ = QStringLiteral("png");
        subtitle_font_family_ = settings.value(
            QStringLiteral("Video/Subtitle/FontFamily"), QStringLiteral("Segoe UI")).toString();
        subtitle_color_ = settings.value(
            QStringLiteral("Video/Subtitle/Color"), QStringLiteral("#ffffff")).toString();
        subtitle_font_size_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/FontSize"), 0).toInt(), 0, 96);
        subtitle_vertical_position_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/VerticalPosition"), 80).toInt(), 60, 80);
        middle_click_action_ = std::clamp(
            settings.value(QStringLiteral("Video/Mouse/MiddleClickAction"), 1).toInt(), 0, 2);
        double_click_action_ = std::clamp(
            settings.value(QStringLiteral("Video/Mouse/DoubleClickAction"), 0).toInt(), 0, 2);
        wheel_action_ = std::clamp(
            settings.value(QStringLiteral("Video/Mouse/WheelAction"), 0).toInt(), 0, 2);
        mouse_gestures_enabled_ = settings.value(
            QStringLiteral("Video/Mouse/RightDragGestures"), false).toBool();
        auto_match_subtitle_ = settings.value(
            QStringLiteral("Video/Subtitle/AutoMatch"), true).toBool();
        subtitles_enabled_ = settings.value(
            QStringLiteral("Video/Subtitle/Enabled"), true).toBool();
        remember_position_ = settings.value(
            QStringLiteral("Video/Playback/RememberPosition"), true).toBool();
        auto_play_next_ = settings.value(
            QStringLiteral("Video/Playback/AutoPlayNext"), true).toBool();
        always_on_top_ = settings.value(
            QStringLiteral("Video/Window/AlwaysOnTop"), false).toBool();
        subtitle_offset_ms_ = settings.value(
            QStringLiteral("Video/Subtitle/OffsetMs"), 0).toLongLong();
    }
    setAcceptDrops(true);
    resize(mode_ == PlayerMode::Video ? QSize(960, 600) : QSize(720, 480));
    setWindowTitle(mode_ == PlayerMode::Video
        ? QStringLiteral("LNPlayer - Video")
        : QStringLiteral("LNPlayer - Audio"));
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        const QByteArray geometry = settings.value(QStringLiteral("Video/Window/Geometry")).toByteArray();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
        setWindowFlag(Qt::WindowStaysOnTopHint, always_on_top_);
        normal_geometry_ = saveGeometry();
    }

    setStyleSheet(QStringLiteral(
        "QMainWindow, QDialog { background: #171a20; color: #e8eaf0; }"
        "QWidget { color: #e8eaf0; }"
        "QMenuBar { background: #20242c; border-bottom: 1px solid #343a46; }"
        "QMenuBar::item { padding: 6px 10px; background: transparent; }"
        "QMenuBar::item:selected { background: #343b48; }"
        "QMenu { background: #252a33; border: 1px solid #424957; padding: 4px; }"
        "QMenu::item { padding: 6px 28px 6px 12px; }"
        "QMenu::item:selected { background: #3c4655; }"
        "QPushButton { background: #2a303a; border: 1px solid #414957; "
        "border-radius: 5px; padding: 6px 12px; }"
        "QPushButton:hover { background: #384252; }"
        "QPushButton:pressed { background: #202630; }"
        "QPushButton:checked { background: #345b86; border-color: #4f8cc8; }"
        "QPushButton:disabled { color: #777f8c; background: #22262d; }"
        "QListWidget, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { "
        "background: #20252d; border: 1px solid #3b4350; border-radius: 4px; "
        "selection-background-color: #345b86; padding: 4px; }"
        "QListWidget::item { padding: 5px; }"
        "QListWidget::item:selected { background: #345b86; }"
        "QSlider::groove:horizontal { height: 4px; background: #424a56; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #4d91cf; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; "
        "background: #e4edf7; border: 1px solid #6c9bc5; border-radius: 6px; }"
        "QTabWidget::pane { border: 1px solid #3b4350; }"
        "QTabBar::tab { background: #252a33; padding: 7px 14px; }"
        "QTabBar::tab:selected { background: #345b86; }"
        "QStatusBar { background: #20242c; border-top: 1px solid #343a46; }"));

    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* open_action = file_menu->addAction(QStringLiteral("打开文件..."));
    connect(open_action, &QAction::triggered, this, [this]
    {
        openFile();
    });
    if (mode_ == PlayerMode::Video)
    {
        auto* open_folder_action = file_menu->addAction(QStringLiteral("打开文件夹..."));
        connect(open_folder_action, &QAction::triggered, this, [this]
        {
            const QString directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("打开视频文件夹"));
            if (directory.isEmpty())
                return;

            const QStringList filters = {
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
            QDirIterator iterator(
                directory, filters, QDir::Files | QDir::Readable,
                QDirIterator::Subdirectories);
            while (iterator.hasNext())
                paths.append(QFileInfo(iterator.next()).absoluteFilePath());
            std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right)
            {
                return left.compare(right, Qt::CaseInsensitive) < 0;
            });

            QList<QUrl> urls;
            for (const QString& path : paths)
                urls.append(QUrl::fromLocalFile(path));
            if (!urls.isEmpty())
                openFiles(urls);
            else
                statusBar()->showMessage(QStringLiteral("文件夹及其子目录中没有支持的视频文件"));
        });
        auto* recent_menu = file_menu->addMenu(QStringLiteral("最近播放"));
        connect(recent_menu, &QMenu::aboutToShow, this, [this, recent_menu]
        {
            recent_menu->clear();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QStringList recent_files = settings.value(
                QStringLiteral("Video/RecentFiles")).toStringList();
            for (const QString& path : recent_files)
            {
                if (!QFileInfo::exists(path))
                    continue;
                QAction* action = recent_menu->addAction(QFileInfo(path).fileName());
                action->setToolTip(path);
                connect(action, &QAction::triggered, this, [this, path]
                {
                    openFiles({QUrl::fromLocalFile(path)});
                });
            }
            if (recent_menu->actions().isEmpty())
            {
                QAction* empty = recent_menu->addAction(QStringLiteral("暂无记录"));
                empty->setEnabled(false);
            }
        });
    }
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

            lnplayer_t* player_handle = player_.load(std::memory_order_acquire);

            if (!player_handle)
                return;

            const QByteArray path_utf8 = directory.toUtf8();
            status_label_->setText(QStringLiteral("正在扫描音乐目录..."));
            postLibrary([this, path_utf8, player_handle]
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
            });
        });
    }

    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QStringLiteral("退出"));
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    if (mode_ == PlayerMode::Video)
    {
        auto* playback_menu = menuBar()->addMenu(QStringLiteral("播放"));
        auto* play_pause_action = playback_menu->addAction(QStringLiteral("播放/暂停"));
        connect(play_pause_action, &QAction::triggered, this, [this] { togglePlayback(); });
        auto* stop_action = playback_menu->addAction(QStringLiteral("停止"));
        connect(stop_action, &QAction::triggered, this, [this]
        {
            postCommand([this]
            {
                if (player_)
                    lnplayer_stop(player_);
            });
        });
        playback_menu->addSeparator();
        auto* previous_action = playback_menu->addAction(QStringLiteral("上一项"));
        auto* next_action = playback_menu->addAction(QStringLiteral("下一项"));
        connect(previous_action, &QAction::triggered, this, [this] { playPrevious(); });
        connect(next_action, &QAction::triggered, this, [this] { playNext(); });

        auto* bookmarks_menu = playback_menu->addMenu(QStringLiteral("书签"));
        connect(bookmarks_menu, &QMenu::aboutToShow, this, [this, bookmarks_menu]
        {
            bookmarks_menu->clear();
            if (current_media_path_.isEmpty())
            {
                QAction* unavailable = bookmarks_menu->addAction(QStringLiteral("请先打开视频"));
                unavailable->setEnabled(false);
                return;
            }

            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QString key = mediaPreferenceKey("Bookmarks", current_media_path_);
            const QVariantList bookmarks = settings.value(key).toList();
            QAction* add_action = bookmarks_menu->addAction(QStringLiteral("添加当前书签..."));
            connect(add_action, &QAction::triggered, this, [this, key, bookmark_count = bookmarks.size()]
            {
                const int64_t bookmark_position = current_position_ms_;
                bool accepted = false;
                const QString default_name = QStringLiteral("书签 %1").arg(bookmark_count + 1);
                const QString name = QInputDialog::getText(
                    this, QStringLiteral("添加书签"), QStringLiteral("名称"),
                    QLineEdit::Normal,
                    QStringLiteral("%1 · %2").arg(default_name, format_position(bookmark_position)),
                    &accepted).trimmed();
                if (!accepted || name.isEmpty())
                    return;

                QSettings store(settings_file_path(), QSettings::IniFormat);
                QVariantList updated = store.value(key).toList();
                QVariantMap entry;
                entry.insert(QStringLiteral("position"), bookmark_position);
                entry.insert(QStringLiteral("name"), name);
                updated.append(entry);
                store.setValue(key, updated);
                store.sync();
                statusBar()->showMessage(QStringLiteral("书签已添加：%1").arg(name));
            });

            bookmarks_menu->addSeparator();
            for (int index = 0; index < bookmarks.size(); ++index)
            {
                const QVariantMap entry = bookmarks[index].toMap();
                const int64_t position = entry.value(QStringLiteral("position")).toLongLong();
                const QString name = entry.value(QStringLiteral("name")).toString();
                auto* bookmark_menu = bookmarks_menu->addMenu(
                    QStringLiteral("%1  %2").arg(name, format_position(position)));
                auto* jump_action = bookmark_menu->addAction(QStringLiteral("跳转"));
                connect(jump_action, &QAction::triggered, this, [this, position, name]
                {
                    seekToPosition(position, QStringLiteral("跳转书签：%1").arg(name));
                });
                auto* remove_action = bookmark_menu->addAction(QStringLiteral("删除"));
                connect(remove_action, &QAction::triggered, this, [this, key, index]
                {
                    QSettings store(settings_file_path(), QSettings::IniFormat);
                    QVariantList updated = store.value(key).toList();
                    if (index >= 0 && index < updated.size())
                    {
                        updated.removeAt(index);
                        store.setValue(key, updated);
                        store.sync();
                    }
                });
            }
            if (bookmarks.isEmpty())
            {
                QAction* empty = bookmarks_menu->addAction(QStringLiteral("暂无书签"));
                empty->setEnabled(false);
            }
            else
            {
                bookmarks_menu->addSeparator();
                auto* clear_action = bookmarks_menu->addAction(QStringLiteral("清空本视频书签"));
                connect(clear_action, &QAction::triggered, this, [this, key]
                {
                    QSettings store(settings_file_path(), QSettings::IniFormat);
                    store.remove(key);
                    store.sync();
                });
            }
        });

        auto* chapter_menu = playback_menu->addMenu(QStringLiteral("章节"));
        connect(chapter_menu, &QMenu::aboutToShow, this, [this, chapter_menu]
        {
            chapter_menu->clear();
            struct ChapterEntry
            {
                int64_t start_ms;
                QString title;
            };
            std::vector<ChapterEntry> chapters;
            if (player_)
            {
                lnplayer_enumerate_chapters(
                    player_,
                    [](const lnplayer_chapter_info_t* chapter, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<ChapterEntry>*>(user_data);
                        entries->push_back({
                            chapter->start_ms,
                            QString::fromUtf8(chapter->title_utf8
                                ? chapter->title_utf8 : "")});
                    },
                    &chapters);
            }

            for (const ChapterEntry& chapter : chapters)
            {
                const QString label = QStringLiteral("%1  %2")
                    .arg(format_position(chapter.start_ms), chapter.title);
                QAction* action = chapter_menu->addAction(label);
                connect(action, &QAction::triggered, this, [this, chapter]
                {
                    seekToPosition(chapter.start_ms,
                        QStringLiteral("跳转章节：%1").arg(chapter.title));
                });
            }
            if (chapters.empty())
            {
                QAction* unavailable = chapter_menu->addAction(QStringLiteral("当前文件没有章节"));
                unavailable->setEnabled(false);
            }
        });

        auto* speed_menu = playback_menu->addMenu(QStringLiteral("播放速度"));
        QList<QAction*> speed_actions;
        const float playback_speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};
        auto* speed_group = new QActionGroup(speed_menu);
        speed_group->setExclusive(true);
        for (float speed : playback_speeds)
        {
            QAction* action = speed_menu->addAction(
                QStringLiteral("%1x").arg(speed, 0, 'f', 2));
            action->setCheckable(true);
            action->setData(speed);
            speed_group->addAction(action);
            speed_actions.append(action);
            connect(action, &QAction::triggered, this, [this, speed]
            {
                setPlaybackRate(speed);
            });
        }
        connect(speed_menu, &QMenu::aboutToShow, this, [this, speed_actions]
        {
            for (QAction* action : speed_actions)
                action->setChecked(std::abs(action->data().toFloat() - playback_rate_) < 0.001f);
        });

        auto* audio_menu = menuBar()->addMenu(QStringLiteral("音轨"));
        connect(audio_menu, &QMenu::aboutToShow, this, [this, audio_menu]
        {
            audio_menu->clear();
            std::vector<std::pair<int, QString>> tracks;
            if (player_)
            {
                lnplayer_enumerate_audio_tracks(
                    player_,
                    [](const lnplayer_audio_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<std::pair<int, QString>>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Audio");
                        if (track->channels > 0)
                            label += QStringLiteral(" (%1 ch)").arg(track->channels);
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        if (track->is_selected)
                            label.prepend(QStringLiteral("✓ "));
                        entries->emplace_back(track->stream_index, label);
                    },
                    &tracks);
            }
            for (const auto& track : tracks)
            {
                QAction* action = audio_menu->addAction(track.second);
                action->setData(track.first);
                connect(action, &QAction::triggered, this, [this, index = track.first]
                {
                    selectAudioTrack(index);
                });
            }
            if (tracks.empty())
            {
                QAction* unavailable = audio_menu->addAction(QStringLiteral("无可用音轨"));
                unavailable->setEnabled(false);
            }
        });

        auto* subtitle_menu = menuBar()->addMenu(QStringLiteral("字幕"));
        auto* embedded_tracks_menu = subtitle_menu->addMenu(QStringLiteral("内嵌字幕"));
        connect(embedded_tracks_menu, &QMenu::aboutToShow, this, [this, embedded_tracks_menu]
        {
            embedded_tracks_menu->clear();
            QAction* disable_action = embedded_tracks_menu->addAction(QStringLiteral("禁用内嵌字幕"));
            connect(disable_action, &QAction::triggered, this, [this]
            {
                selectSubtitleTrack(-1);
            });
            embedded_tracks_menu->addSeparator();
            struct TrackEntry
            {
                int index;
                QString label;
            };
            std::vector<TrackEntry> tracks;
            if (player_)
            {
                lnplayer_enumerate_subtitle_tracks(
                    player_,
                    [](const lnplayer_subtitle_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<TrackEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Subtitle");
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        if (track->is_selected)
                            label.prepend(QStringLiteral("✓ "));
                        entries->push_back({track->stream_index, label});
                    },
                    &tracks);
            }
            for (const auto& track : tracks)
            {
                QAction* action = embedded_tracks_menu->addAction(track.label);
                connect(action, &QAction::triggered, this, [this, index = track.index]
                {
                    selectSubtitleTrack(index);
                });
            }
            if (tracks.empty())
            {
                QAction* unavailable = embedded_tracks_menu->addAction(QStringLiteral("无可用内嵌字幕"));
                unavailable->setEnabled(false);
            }
        });
        auto* load_subtitle_action = subtitle_menu->addAction(QStringLiteral("加载外挂字幕..."));
        connect(load_subtitle_action, &QAction::triggered, this, [this] { loadExternalSubtitle(); });
        auto* subtitles_enabled_action = subtitle_menu->addAction(QStringLiteral("启用字幕"));
        subtitles_enabled_action->setCheckable(true);
        subtitles_enabled_action->setChecked(subtitles_enabled_);
        connect(subtitle_menu, &QMenu::aboutToShow, this, [this, subtitles_enabled_action]
        {
            subtitles_enabled_action->setChecked(subtitles_enabled_);
        });
        connect(subtitles_enabled_action, &QAction::toggled, this, [this](bool enabled)
        {
            subtitles_enabled_ = enabled;
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/Subtitle/Enabled"), enabled);
            settings.sync();
            postCommand([this, enabled]
            {
                if (player_)
                    lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
            });
            pending_video_frame_valid_ = false;
            requestVideoFrame();
            showOsd(enabled ? QStringLiteral("字幕已启用") : QStringLiteral("字幕已关闭"));
        });
        subtitle_menu->addSeparator();
        auto* subtitle_delay_action = subtitle_menu->addAction(QStringLiteral("字幕延迟 +500 ms"));
        auto* subtitle_advance_action = subtitle_menu->addAction(QStringLiteral("字幕提前 500 ms"));
        auto* subtitle_reset_action = subtitle_menu->addAction(QStringLiteral("重置字幕时间"));
        connect(subtitle_delay_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(subtitle_offset_ms_ + 500); });
        connect(subtitle_advance_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(subtitle_offset_ms_ - 500); });
        connect(subtitle_reset_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(0); });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* video_menu = menuBar()->addMenu(QStringLiteral("视频"));
        auto* playback_info_action = video_menu->addAction(QStringLiteral("播放信息..."));
        connect(playback_info_action, &QAction::triggered, this, [this] { showPlaybackInfo(); });
        auto* screenshot_action = video_menu->addAction(QStringLiteral("截图..."));
        connect(screenshot_action, &QAction::triggered, this, [this] { takeScreenshot(); });
        always_on_top_action_ = video_menu->addAction(QStringLiteral("窗口置顶"));
        always_on_top_action_->setCheckable(true);
        always_on_top_action_->setChecked(always_on_top_);
        connect(always_on_top_action_, &QAction::toggled, this, [this](bool enabled)
        {
            always_on_top_ = enabled;
            const bool was_fullscreen = isFullScreen();
            const bool was_maximized = isMaximized();
            setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
            if (was_fullscreen)
                showFullScreen();
            else if (was_maximized)
                showMaximized();
            else
                show();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), enabled);
            settings.sync();
        });
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
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), visible);
            settings.sync();
            if (visible && isFullScreen())
                showFullscreenControls();
        });
        auto* clear_playlist_action = playlist_menu->addAction(QStringLiteral("清空播放列表"));
        connect(clear_playlist_action, &QAction::triggered, this, [this]
        {
            clearPlaylist();
        });
        playlist_menu->addSeparator();
        auto* detached_playlist_action =
            playlist_menu->addAction(QStringLiteral("在独立窗口中打开播放列表..."));
        connect(detached_playlist_action, &QAction::triggered,
            this, [this] { showPlaylistWindow(); });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* settings_menu = menuBar()->addMenu(QStringLiteral("设置"));
        auto* video_settings_action = settings_menu->addAction(QStringLiteral("播放器设置..."));
        connect(video_settings_action, &QAction::triggered, this, [this] { openVideoSettings(); });
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
        empty_state_label_ = new QLabel(
            QStringLiteral("拖放视频文件到此处，或从“文件”菜单打开视频"),
            video_surface_);
        empty_state_label_->setAlignment(Qt::AlignCenter);
        empty_state_label_->setWordWrap(true);
        empty_state_label_->setMaximumWidth(520);
        empty_state_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        empty_state_label_->setStyleSheet(QStringLiteral(
            "QLabel { color: #c7ced8; background: rgba(0, 0, 0, 145); "
            "border-radius: 10px; padding: 16px 24px; font-size: 16px; }"));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        osd_timer_->setSingleShot(true);
        connect(osd_timer_, &QTimer::timeout, osd_label_, &QWidget::hide);
        {
            QSettings settings(settings_file_path(), QSettings::IniFormat);
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
        playlist_widget_->setItemDelegate(new PlaylistItemDelegate(playlist_widget_));
        playlist_widget_->setMaximumWidth(360);
        playlist_widget_->setDragDropMode(QAbstractItemView::InternalMove);
        playlist_widget_->setDropIndicatorShown(true);
        playlist_widget_->setAcceptDrops(true);
        playlist_widget_->installEventFilter(this);
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
        playlist_panel_layout->addWidget(create_playlist_header(playlist_panel_));
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
            menu.setMouseTracking(true);
            menu.installEventFilter(this);
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
                if (double_click_action_ == 1)
                    togglePlayback();
                else if (double_click_action_ == 0)
                {
                    if (isFullScreen())
                        showNormal();
                    else
                        showFullScreen();
                }
            },
            [this](int direction, bool control)
            {
                if (control || wheel_action_ == 2)
                {
                    video_surface_->adjust_zoom(direction);
                    showOsd(QStringLiteral("画面缩放 %1%")
                        .arg(static_cast<int>(video_surface_->zoom() * 100.0f)));
                    return;
                }
                if (wheel_action_ == 1)
                {
                    seekRelative(static_cast<int64_t>(direction) * 5000);
                    return;
                }
                volume_slider_->setValue(std::clamp(
                    volume_slider_->value() + direction * 50, 0, 1000));
            },
            [this]
            {
                if (middle_click_action_ == 1)
                    togglePlayback();
                else if (middle_click_action_ == 2)
                {
                    if (isFullScreen())
                        showNormal();
                    else
                        showFullScreen();
                }
            },
            [this](int direction)
            {
                if (direction == 0)
                    seekRelative(-5000);
                else if (direction == 1)
                    seekRelative(5000);
                else if (direction == 2)
                    volume_slider_->setValue(std::clamp(volume_slider_->value() + 50, 0, 1000));
                else
                    volume_slider_->setValue(std::clamp(volume_slider_->value() - 50, 0, 1000));
            });
        video_surface_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(video_surface_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point)
        {
            if (video_surface_->consume_gesture_context_menu())
                return;

            QMenu menu(this);
            menu.setMouseTracking(true);
            menu.installEventFilter(this);
            QAction* play_pause = menu.addAction(is_playing_
                ? QStringLiteral("暂停")
                : QStringLiteral("播放"));
            QAction* stop = menu.addAction(QStringLiteral("停止"));
            menu.addSeparator();
            QAction* reset_zoom = menu.addAction(QStringLiteral("重置画面缩放"));
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
            QAction* set_loop_a = menu.addAction(QStringLiteral("设置 A-B 循环起点"));
            QAction* set_loop_b = menu.addAction(QStringLiteral("设置 A-B 循环终点"));
            QAction* clear_loop = menu.addAction(QStringLiteral("清除 A-B 循环"));
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
            else if (selected == reset_zoom)
            {
                video_surface_->adjust_zoom(-100);
                showOsd(QStringLiteral("画面缩放 100%"));
            }
            else if (selected == stop)
                postCommand([this]
                {
                    if (player_)
                        lnplayer_stop(player_);
                });
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
                selectAudioTrack(selected->data().toInt());
            else if (selected == disable_embedded_subtitle ||
                (selected && selected->parent() == embedded_subtitle_menu))
            {
                selectSubtitleTrack(selected == disable_embedded_subtitle
                    ? -1
                    : selected->data().toInt());
            }
            else if (selected == subtitles_enabled)
            {
                subtitles_enabled_ = subtitles_enabled->isChecked();
                const bool enabled = subtitles_enabled_;
                postCommand([this, enabled]
                {
                    if (player_)
                        lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
                });
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }
            else if (selected == subtitle_delay)
                setSubtitleOffset(subtitle_offset_ms_ + 500);
            else if (selected == subtitle_advance)
                setSubtitleOffset(subtitle_offset_ms_ - 500);
            else if (selected == subtitle_reset)
                setSubtitleOffset(0);
            else if (selected == previous_frame)
                stepVideoFrame(true);
            else if (selected == next_frame)
                stepVideoFrame();
            else if (selected == set_loop_a)
            {
                loop_a_ms_ = current_position_ms_;
                if (loop_b_ms_ <= loop_a_ms_)
                    loop_b_ms_ = -1;
                showOsd(QStringLiteral("A 点：%1").arg(format_position(loop_a_ms_)));
            }
            else if (selected == set_loop_b)
            {
                if (loop_a_ms_ < 0 || current_position_ms_ <= loop_a_ms_)
                {
                    statusBar()->showMessage(QStringLiteral("请先设置有效的 A 点"));
                }
                else
                {
                    loop_b_ms_ = current_position_ms_;
                    showOsd(QStringLiteral("A-B 循环：%1 - %2")
                        .arg(format_position(loop_a_ms_), format_position(loop_b_ms_)));
                }
            }
            else if (selected == clear_loop)
            {
                loop_a_ms_ = -1;
                loop_b_ms_ = -1;
                showOsd(QStringLiteral("已清除 A-B 循环"));
            }
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
                QSettings settings(settings_file_path(), QSettings::IniFormat);
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
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
                settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
                settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
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
    seek_slider_->setMouseTracking(true);
    seek_slider_->installEventFilter(this);
    seek_slider_->setEnabled(false);
    volume_slider_->setRange(0, 1000);
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
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
        if (statusBar())
            statusBar()->hide();
        if (playlist_panel_)
            playlist_panel_->hide();
        setCursor(Qt::BlankCursor);
    });
    if (video_surface_)
    {
        video_surface_->setMouseTracking(true);
        video_surface_->set_gesture_enabled(mouse_gestures_enabled_);
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
    if (playlist_panel_)
    {
        playlist_panel_->setMouseTracking(true);
        playlist_panel_->installEventFilter(this);
        const auto playlist_children = playlist_panel_->findChildren<QWidget*>();
        for (QWidget* child : playlist_children)
        {
            child->setMouseTracking(true);
            child->installEventFilter(this);
        }
    }

    if (mode_ == PlayerMode::Video)
    {
        auto shortcut_focus_blocked = [](QWidget* focus)
        {
            return QApplication::activePopupWidget() || QApplication::activeModalWidget() ||
                qobject_cast<QLineEdit*>(focus) ||
                qobject_cast<QPlainTextEdit*>(focus) ||
                qobject_cast<QTextEdit*>(focus) ||
                qobject_cast<QAbstractItemView*>(focus) ||
                qobject_cast<QAbstractButton*>(focus) ||
                qobject_cast<QAbstractSlider*>(focus) ||
                qobject_cast<QAbstractSpinBox*>(focus) ||
                qobject_cast<QComboBox*>(focus) ||
                qobject_cast<QKeySequenceEdit*>(focus);
        };
        auto bind_shortcut = [this, shortcut_focus_blocked](const QString& id, const QString& label,
            int key, std::function<void()> action)
        {
            const QKeySequence default_sequence(key);
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QString setting_key = QStringLiteral("Video/Shortcuts/") + id;
            const QKeySequence sequence = QKeySequence::fromString(
                settings.value(setting_key, default_sequence.toString()).toString());
            auto* shortcut = new QShortcut(sequence, this);
            shortcut->setContext(Qt::WindowShortcut);
            shortcut->setProperty("commandLabel", label);
            shortcut->setProperty("defaultSequence", default_sequence.toString());
            shortcuts_.insert(id, shortcut);
            connect(shortcut, &QShortcut::activated, this,
                [action = std::move(action), shortcut_focus_blocked]
            {
                if (shortcut_focus_blocked(QApplication::focusWidget()))
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
        bind_shortcut(QStringLiteral("toggle_topmost"), QStringLiteral("窗口置顶"), Qt::CTRL | Qt::Key_T, [this]
        {
            if (always_on_top_action_)
                always_on_top_action_->setChecked(!always_on_top_action_->isChecked());
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

        const auto update_shortcut_availability =
            [this, shortcut_focus_blocked](QWidget*, QWidget* focus)
        {
            const bool enabled = !shortcut_focus_blocked(focus);
            for (QShortcut* shortcut : shortcuts_)
                shortcut->setEnabled(enabled);
        };
        connect(qApp, &QApplication::focusChanged, this, update_shortcut_availability);
        update_shortcut_availability(nullptr, QApplication::focusWidget());
    }
    statusBar()->showMessage(QStringLiteral("正在初始化播放器"));

    ui_io_thread_ = std::thread([this]
    {
        ui_io_context_.run();
    });

    const uint32_t initial_volume = static_cast<uint32_t>(volume_slider_->value());
    const float initial_playback_rate = playback_rate_;
    const int64_t initial_subtitle_offset_ms = subtitle_offset_ms_;
    const bool initial_subtitles_enabled = subtitles_enabled_;
    void* initial_video_device = mode_ == PlayerMode::Video && video_surface_
        ? video_surface_->native_device()
        : nullptr;
    video_device_identity_ = initial_video_device;
    postCommand([this, initial_volume, initial_playback_rate, initial_subtitle_offset_ms, initial_subtitles_enabled, initial_video_device]
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

        lnplayer_t* created_player = nullptr;
        const lnplayer_result_t result = lnplayer_create(&config, &created_player);
        if (result == LNPLAYER_RESULT_OK)
        {
            player_.store(created_player, std::memory_order_release);
            lnplayer_set_volume(created_player, initial_volume);
            if (mode_ == PlayerMode::Video)
            {
                lnplayer_set_playback_rate(created_player, initial_playback_rate);
                lnplayer_set_subtitle_offset(created_player, initial_subtitle_offset_ms);
                lnplayer_set_subtitles_enabled(created_player, initial_subtitles_enabled ? 1 : 0);
            }
        }

        if (initial_video_device)
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                const lnplayer_result_t device_result =
                    lnplayer_windows_set_d3d11_device(created_player, initial_video_device);
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
    });

    connect(play_button_, &QPushButton::clicked, this, [this]
    {
        if (video_seek_pending_)
            return;
        postCommand([this]
        {
            if (player_)
                lnplayer_play(player_);
        });
    });

    connect(pause_button_, &QPushButton::clicked, this, [this]
    {
        postCommand([this]
        {
            if (player_)
                lnplayer_pause(player_);
        });
    });

    connect(stop_button_, &QPushButton::clicked, this, [this]
    {
        postCommand([this]
        {
            if (player_)
                lnplayer_stop(player_);
        });
    });

    volume_settings_timer_->setSingleShot(true);
    volume_settings_timer_->setInterval(300);
    connect(volume_settings_timer_, &QTimer::timeout, this, [this]
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Playback/Volume"), volume_slider_->value());
        settings.setValue(QStringLiteral("Playback/MutedVolume"), muted_volume_);
        settings.sync();
    });

    connect(volume_slider_, &QSlider::valueChanged, this, [this](int value)
    {
        if (value > 0)
            muted_volume_ = value;
        volume_settings_timer_->start();

        showOsd(value == 0 ? QStringLiteral("静音")
            : QStringLiteral("音量 %1%").arg(value / 10));
        postCommand([this, value]
        {
            if (player_)
                lnplayer_set_volume(player_, static_cast<uint32_t>(value));
        });
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
    });

    snapshot_timer_->setInterval(250);
    connect(snapshot_timer_, &QTimer::timeout, this, &MainWindow::refreshSnapshot);

    video_timer_->setInterval(4);
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
            postCommand([this, current_device]
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
            });
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
    volume_settings_timer_->stop();
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Playback/Volume"), volume_slider_->value());
        settings.setValue(QStringLiteral("Playback/MutedVolume"), muted_volume_);
        settings.sync();
    }
    savePlaybackPosition();
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Video/Window/Geometry"),
            normal_geometry_.isEmpty() ? saveGeometry() : normal_geometry_);
        settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), always_on_top_);
        settings.sync();
    }
    saveVideoPlaylist();
    snapshot_timer_->stop();
    video_timer_->stop();
    device_timer_->stop();

    std::promise<void> io_barrier;
    std::future<void> io_barrier_future = io_barrier.get_future();
    asio::post(ui_io_context_, [&io_barrier]
    {
        io_barrier.set_value();
    });
    io_barrier_future.wait();
    ui_io_work_guard_.reset();
    if (ui_io_thread_.joinable())
        ui_io_thread_.join();

    // The dispatcher has submitted every accepted task. Drain the shared pool
    // before destroying the Player handle used by those tasks.
    ui_thread_pool_.join();
    if (lnplayer_t* player = player_.exchange(nullptr, std::memory_order_acq_rel))
        lnplayer_destroy(player);
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
        showFullscreenControls();
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

void MainWindow::showFullscreenControls()
{
    if (!isFullScreen() || !control_panel_)
        return;

    unsetCursor();
    control_panel_->show();
    if (menuBar())
        menuBar()->show();
    if (statusBar())
        statusBar()->show();
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
        if (statusBar())
            statusBar()->show();
        if (playlist_panel_)
            playlist_panel_->setVisible(playlist_panel_visible_);
        fullscreen_controls_visible_ = true;
        return;
    }

    if (control_panel_)
        control_panel_->show();
    if (menuBar())
        menuBar()->show();
    if (statusBar())
        statusBar()->show();
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

    if (isFullScreen())
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
            watched == menuBar() || (menuBar() && watched_widget && menuBar()->isAncestorOf(watched_widget)) ||
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
        if (watched == video_surface_ && event->type() == QEvent::Resize &&
            osd_label_ && osd_label_->isVisible())
            positionOsd();
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
            QWidget* cursor_widget = QApplication::widgetAt(QCursor::pos());
            const bool pointer_over_controls = cursor_widget &&
                ((control_panel_ &&
                    (cursor_widget == control_panel_ || control_panel_->isAncestorOf(cursor_widget))) ||
                 (playlist_panel_ &&
                    (cursor_widget == playlist_panel_ || playlist_panel_->isAncestorOf(cursor_widget))) ||
                 (menuBar() &&
                    (cursor_widget == menuBar() || menuBar()->isAncestorOf(cursor_widget))) ||
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
        updateFullscreenUi();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
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
    if (urls.isEmpty())
        return;

    savePlaybackPosition();
    status_label_->setText(QStringLiteral("正在扫描媒体文件..."));
    postLibrary([this, urls]
    {
        const QStringList paths = expand_media_urls(urls);
        QMetaObject::invokeMethod(this, [this, paths]
        {
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

    postLibrary([this, urls]
    {
        const QStringList paths = expand_media_urls(urls);
        QMetaObject::invokeMethod(this, [this, paths]
        {
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

void MainWindow::openVideoSettings()
{
    if (mode_ != PlayerMode::Video)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("LNPlayer 播放器设置"));
    dialog.setMinimumSize(820, 600);
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    auto* content = new QHBoxLayout();
    content->setSpacing(14);
    auto* navigation = new QListWidget(&dialog);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setSelectionMode(QAbstractItemView::SingleSelection);
    navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    navigation->setFixedWidth(156);
    navigation->setSpacing(3);
    auto* pages = new QStackedWidget(&dialog);
    pages->setObjectName(QStringLiteral("settingsPages"));
    content->addWidget(navigation);
    content->addWidget(pages, 1);
    root->addLayout(content, 1);

    auto add_settings_page = [navigation, pages](const QString& title, QWidget* page)
    {
        auto* item = new QListWidgetItem(title, navigation);
        item->setSizeHint(QSize(136, 38));
        auto* scroll = new QScrollArea(pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
        pages->addWidget(scroll);
    };
    connect(navigation, &QListWidget::currentRowChanged,
        pages, &QStackedWidget::setCurrentIndex);
    navigation->setStyleSheet(QStringLiteral(
        "QListWidget { background: #20252d; border: 1px solid #343c49; "
        "border-radius: 7px; padding: 5px; outline: 0; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 5px; }"
        "QListWidget::item:hover { background: #303846; }"
        "QListWidget::item:selected { background: #345b86; color: #ffffff; }"));

    auto* general_page = new QWidget(pages);
    auto* general_form = new QFormLayout(general_page);
    auto* settings_path = new QLineEdit(settings_file_path(), general_page);
    settings_path->setReadOnly(true);
    auto* open_settings_directory = new QPushButton(
        QStringLiteral("打开配置目录"), general_page);
    connect(open_settings_directory, &QPushButton::clicked, &dialog, []
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(settings_file_path()).absolutePath()));
    });
    general_form->addRow(QStringLiteral("INI 配置文件"), settings_path);
    general_form->addRow(open_settings_directory);
    auto* association_info = new QLabel(
        QStringLiteral("安装版会为当前 Windows 用户注册支持的视频格式，不修改其他用户的关联。"
            "Windows 仍负责默认应用选择；便携版可通过系统“打开方式”选择 LNPlayer。"
            "支持：MP4、MKV、MOV、AVI、WebM、WMV、FLV、MPEG、MPG、M4V、TS、MTS、"
            "M2TS、3GP、3G2、VOB、OGV、MXF、ASF、F4V、DIVX、MPE、MPV。"),
        general_page);
    association_info->setWordWrap(true);
    auto* open_default_apps = new QPushButton(
        QStringLiteral("打开 Windows 默认应用设置"), general_page);
    connect(open_default_apps, &QPushButton::clicked, &dialog, []
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=LNPlayer")));
    });
    general_form->addRow(QStringLiteral("视频文件关联"), association_info);
    general_form->addRow(open_default_apps);
    add_settings_page(QStringLiteral("常规"), general_page);

    auto* playback_page = new QWidget(pages);
    auto* playback_form = new QFormLayout(playback_page);
    auto* remember_position = new QCheckBox(
        QStringLiteral("记住每个视频上次播放位置"), playback_page);
    remember_position->setChecked(remember_position_);
    auto* auto_play_next = new QCheckBox(
        QStringLiteral("当前视频结束后自动播放下一项"), playback_page);
    auto_play_next->setChecked(auto_play_next_);
    auto* default_playback_rate = new QDoubleSpinBox(playback_page);
    default_playback_rate->setRange(0.25, 4.0);
    default_playback_rate->setSingleStep(0.25);
    default_playback_rate->setDecimals(2);
    default_playback_rate->setSuffix(QStringLiteral(" ×"));
    default_playback_rate->setValue(playback_rate_);
    auto* repeat_mode = new QComboBox(playback_page);
    repeat_mode->addItem(QStringLiteral("关闭循环"), 0);
    repeat_mode->addItem(QStringLiteral("列表循环"), 1);
    repeat_mode->addItem(QStringLiteral("单项循环"), 2);
    repeat_mode->setCurrentIndex(repeat_mode_);
    auto* shuffle_enabled = new QCheckBox(QStringLiteral("启用随机播放"), playback_page);
    shuffle_enabled->setChecked(shuffle_enabled_);
    playback_form->addRow(remember_position);
    playback_form->addRow(auto_play_next);
    playback_form->addRow(QStringLiteral("默认播放速度"), default_playback_rate);
    playback_form->addRow(QStringLiteral("循环模式"), repeat_mode);
    playback_form->addRow(shuffle_enabled);
    add_settings_page(QStringLiteral("播放"), playback_page);

    auto* interface_page = new QWidget(pages);
    auto* interface_form = new QFormLayout(interface_page);
    auto* hide_delay = new QSpinBox(interface_page);
    hide_delay->setRange(0, 10);
    hide_delay->setSuffix(QStringLiteral(" 秒"));
    hide_delay->setSpecialValueText(QStringLiteral("关闭"));
    hide_delay->setValue(fullscreen_hide_delay_ms_ / 1000);
    auto* osd_duration = new QSpinBox(interface_page);
    osd_duration->setRange(500, 5000);
    osd_duration->setSingleStep(250);
    osd_duration->setSuffix(QStringLiteral(" ms"));
    osd_duration->setValue(osd_duration_ms_);
    auto* osd_position = new QComboBox(interface_page);
    osd_position->addItem(QStringLiteral("顶部"), 0);
    osd_position->addItem(QStringLiteral("居中"), 1);
    osd_position->addItem(QStringLiteral("底部"), 2);
    osd_position->setCurrentIndex(osd_position_);
    auto* show_playlist = new QCheckBox(QStringLiteral("显示播放列表侧栏"), interface_page);
    show_playlist->setChecked(playlist_panel_visible_);
    auto* always_on_top = new QCheckBox(QStringLiteral("窗口始终置顶"), interface_page);
    always_on_top->setChecked(always_on_top_);
    auto* middle_click = new QComboBox(interface_page);
    middle_click->addItem(QStringLiteral("无动作"), 0);
    middle_click->addItem(QStringLiteral("播放/暂停"), 1);
    middle_click->addItem(QStringLiteral("切换全屏"), 2);
    middle_click->setCurrentIndex(middle_click_action_);
    auto* double_click = new QComboBox(interface_page);
    double_click->addItem(QStringLiteral("切换全屏"), 0);
    double_click->addItem(QStringLiteral("播放/暂停"), 1);
    double_click->addItem(QStringLiteral("无动作"), 2);
    double_click->setCurrentIndex(double_click_action_);
    auto* mouse_gestures = new QCheckBox(
        QStringLiteral("启用右键拖动手势（左/右：快退/快进，上/下：音量）"), interface_page);
    mouse_gestures->setChecked(mouse_gestures_enabled_);
    auto* wheel_action = new QComboBox(interface_page);
    wheel_action->addItem(QStringLiteral("调节音量"), 0);
    wheel_action->addItem(QStringLiteral("快退/快进 5 秒"), 1);
    wheel_action->addItem(QStringLiteral("缩放画面"), 2);
    wheel_action->setCurrentIndex(wheel_action_);
    interface_form->addRow(QStringLiteral("全屏控制栏自动隐藏"), hide_delay);
    interface_form->addRow(QStringLiteral("OSD 显示时长"), osd_duration);
    interface_form->addRow(QStringLiteral("OSD 位置"), osd_position);
    interface_form->addRow(show_playlist);
    interface_form->addRow(always_on_top);
    interface_form->addRow(QStringLiteral("画面中键操作"), middle_click);
    interface_form->addRow(QStringLiteral("画面双击操作"), double_click);
    interface_form->addRow(QStringLiteral("鼠标滚轮操作"), wheel_action);
    interface_form->addRow(mouse_gestures);
    add_settings_page(QStringLiteral("界面"), interface_page);

    auto* video_page = new QWidget(pages);
    auto* video_form = new QFormLayout(video_page);
    auto* aspect = new QComboBox(video_page);
    aspect->addItem(QStringLiteral("原始比例"), 0);
    aspect->addItem(QStringLiteral("16:9"), 1);
    aspect->addItem(QStringLiteral("4:3"), 2);
    aspect->addItem(QStringLiteral("拉伸填充"), 3);
    aspect->addItem(QStringLiteral("自定义比例"), 4);
    const int aspect_index = video_stretch_ ? 3
        : std::abs(video_aspect_ratio_override_ - 16.0f / 9.0f) < 0.001f ? 1
        : std::abs(video_aspect_ratio_override_ - 4.0f / 3.0f) < 0.001f ? 2
        : video_aspect_ratio_override_ > 0.0f ? 4 : 0;
    aspect->setCurrentIndex(aspect_index);
    auto* custom_aspect = new QDoubleSpinBox(video_page);
    custom_aspect->setRange(0.5, 3.0);
    custom_aspect->setSingleStep(0.01);
    custom_aspect->setDecimals(3);
    custom_aspect->setSuffix(QStringLiteral(" : 1"));
    custom_aspect->setValue(video_aspect_ratio_override_ > 0.0f
        ? video_aspect_ratio_override_ : 16.0 / 9.0);
    custom_aspect->setEnabled(aspect_index == 4);
    connect(aspect, qOverload<int>(&QComboBox::currentIndexChanged),
        custom_aspect, [aspect, custom_aspect](int)
        {
            custom_aspect->setEnabled(aspect->currentData().toInt() == 4);
        });

    auto* filter_page = new QWidget(pages);
    auto* filter_form = new QFormLayout(filter_page);
    auto make_filter_spin = [&filter_page](double value, double minimum, double maximum, double step)
    {
        auto* spin = new QDoubleSpinBox(filter_page);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    };
    auto* brightness = make_filter_spin(video_brightness_, -1.0, 1.0, 0.05);
    auto* contrast = make_filter_spin(video_contrast_, 0.0, 2.0, 0.05);
    auto* saturation = make_filter_spin(video_saturation_, 0.0, 2.0, 0.05);
    auto* hue = new QSpinBox(filter_page);
    hue->setRange(-180, 180);
    hue->setSingleStep(5);
    hue->setSuffix(QStringLiteral("°"));
    hue->setValue(video_hue_degrees_);
    auto* sharpness = make_filter_spin(video_sharpness_, 0.0, 2.0, 0.05);
    auto* denoise = make_filter_spin(video_denoise_, 0.0, 1.0, 0.05);
    auto* grayscale = new QCheckBox(QStringLiteral("灰度画面"), filter_page);
    grayscale->setChecked(video_grayscale_);
    video_form->addRow(QStringLiteral("画面比例"), aspect);
    video_form->addRow(QStringLiteral("自定义比例"), custom_aspect);
    add_settings_page(QStringLiteral("视频显示"), video_page);
    filter_form->addRow(QStringLiteral("亮度"), brightness);
    filter_form->addRow(QStringLiteral("对比度"), contrast);
    filter_form->addRow(QStringLiteral("饱和度"), saturation);
    filter_form->addRow(QStringLiteral("色调"), hue);
    filter_form->addRow(QStringLiteral("锐化"), sharpness);
    filter_form->addRow(QStringLiteral("降噪"), denoise);
    filter_form->addRow(grayscale);
    add_settings_page(QStringLiteral("画面滤镜"), filter_page);

    auto* subtitle_page = new QWidget(pages);
    auto* subtitle_form = new QFormLayout(subtitle_page);
    auto* subtitle_enabled = new QCheckBox(
        QStringLiteral("启用字幕显示"), subtitle_page);
    subtitle_enabled->setChecked(subtitles_enabled_);
    auto* auto_match_subtitle = new QCheckBox(
        QStringLiteral("自动加载同名外挂字幕"), subtitle_page);
    auto_match_subtitle->setChecked(auto_match_subtitle_);
    auto* subtitle_offset = new QSpinBox(subtitle_page);
    subtitle_offset->setRange(-60000, 60000);
    subtitle_offset->setSingleStep(100);
    subtitle_offset->setSuffix(QStringLiteral(" ms"));
    subtitle_offset->setValue(static_cast<int>(subtitle_offset_ms_));
    auto* subtitle_style_page = new QWidget(pages);
    auto* subtitle_style_form = new QFormLayout(subtitle_style_page);
    auto* subtitle_font = new QFontComboBox(subtitle_style_page);
    subtitle_font->setCurrentFont(QFont(subtitle_font_family_));
    auto* subtitle_size = new QSpinBox(subtitle_style_page);
    subtitle_size->setRange(0, 96);
    subtitle_size->setSpecialValueText(QStringLiteral("自动"));
    subtitle_size->setSuffix(QStringLiteral(" px"));
    subtitle_size->setValue(subtitle_font_size_);
    auto* subtitle_color = new QComboBox(subtitle_style_page);
    subtitle_color->addItem(QStringLiteral("白色"), QStringLiteral("#ffffff"));
    subtitle_color->addItem(QStringLiteral("黄色"), QStringLiteral("#ffff00"));
    subtitle_color->addItem(QStringLiteral("青色"), QStringLiteral("#00ffff"));
    subtitle_color->addItem(QStringLiteral("绿色"), QStringLiteral("#00ff00"));
    int subtitle_color_index = subtitle_color->findData(subtitle_color_);
    subtitle_color->setCurrentIndex(subtitle_color_index >= 0 ? subtitle_color_index : 0);
    auto* subtitle_position = new QSpinBox(subtitle_style_page);
    subtitle_position->setRange(60, 80);
    subtitle_position->setSuffix(QStringLiteral(" %"));
    subtitle_position->setValue(subtitle_vertical_position_);
    subtitle_form->addRow(subtitle_enabled);
    subtitle_form->addRow(auto_match_subtitle);
    subtitle_form->addRow(QStringLiteral("默认时间偏移"), subtitle_offset);
    add_settings_page(QStringLiteral("字幕加载"), subtitle_page);
    subtitle_style_form->addRow(QStringLiteral("字体"), subtitle_font);
    subtitle_style_form->addRow(QStringLiteral("字号"), subtitle_size);
    subtitle_style_form->addRow(QStringLiteral("颜色"), subtitle_color);
    subtitle_style_form->addRow(QStringLiteral("垂直位置"), subtitle_position);
    add_settings_page(QStringLiteral("字幕样式"), subtitle_style_page);

    auto* shortcut_page = new QWidget(pages);
    auto* shortcut_layout = new QVBoxLayout(shortcut_page);
    auto* shortcut_search = new QLineEdit(shortcut_page);
    shortcut_search->setPlaceholderText(QStringLiteral("搜索快捷键名称或按键..."));
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
    connect(shortcut_search, &QLineEdit::textChanged, shortcut_page,
        [shortcut_form, shortcut_edits](const QString& text)
        {
            const QString query = text.trimmed();
            for (auto it = shortcut_edits.cbegin(); it != shortcut_edits.cend(); ++it)
            {
                auto* label = qobject_cast<QLabel*>(shortcut_form->labelForField(it.value()));
                const QString command = label ? label->text() : QString();
                const QString sequence = it.value()->keySequence().toString(
                    QKeySequence::NativeText);
                const bool visible = query.isEmpty() ||
                    command.contains(query, Qt::CaseInsensitive) ||
                    sequence.contains(query, Qt::CaseInsensitive);
                if (label)
                    label->setVisible(visible);
                it.value()->setVisible(visible);
            }
        });
    auto* shortcut_scroll = new QScrollArea(shortcut_page);
    shortcut_scroll->setWidgetResizable(true);
    shortcut_scroll->setWidget(shortcut_form_widget);
    shortcut_layout->addWidget(shortcut_search);
    shortcut_layout->addWidget(shortcut_scroll);
    add_settings_page(QStringLiteral("快捷键"), shortcut_page);

    auto* screenshot_page = new QWidget(pages);
    auto* screenshot_form = new QFormLayout(screenshot_page);
    auto* screenshot_directory = new QLineEdit(screenshot_directory_, screenshot_page);
    auto* browse_screenshot_directory = new QPushButton(
        QStringLiteral("浏览..."), screenshot_page);
    auto* screenshot_directory_row = new QWidget(screenshot_page);
    auto* screenshot_directory_layout = new QHBoxLayout(screenshot_directory_row);
    screenshot_directory_layout->setContentsMargins(0, 0, 0, 0);
    screenshot_directory_layout->addWidget(screenshot_directory, 1);
    screenshot_directory_layout->addWidget(browse_screenshot_directory);
    connect(browse_screenshot_directory, &QPushButton::clicked, &dialog,
        [&, screenshot_directory]
    {
        const QString directory = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择截图保存目录"),
            screenshot_directory->text().isEmpty()
                ? QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))
                : screenshot_directory->text());
        if (!directory.isEmpty())
            screenshot_directory->setText(directory);
    });
    auto* screenshot_format = new QComboBox(screenshot_page);
    screenshot_format->addItem(QStringLiteral("PNG（无损）"), QStringLiteral("png"));
    screenshot_format->addItem(QStringLiteral("JPEG（压缩）"), QStringLiteral("jpg"));
    screenshot_format->addItem(QStringLiteral("BMP（无压缩）"), QStringLiteral("bmp"));
    const int screenshot_format_index = screenshot_format->findData(screenshot_format_);
    screenshot_format->setCurrentIndex(screenshot_format_index >= 0
        ? screenshot_format_index : 0);
    screenshot_form->addRow(QStringLiteral("默认保存目录"), screenshot_directory_row);
    screenshot_form->addRow(QStringLiteral("默认图片格式"), screenshot_format);
    add_settings_page(QStringLiteral("截图"), screenshot_page);

    auto* performance_page = new QWidget(pages);
    auto* performance_form = new QFormLayout(performance_page);
    const QString renderer_description = video_surface_ && video_surface_->has_renderer()
        ? QStringLiteral("D3D11 renderer 已初始化")
        : QStringLiteral("D3D11 renderer 未初始化");
    performance_form->addRow(QStringLiteral("渲染器"), new QLabel(renderer_description, performance_page));
    performance_form->addRow(QStringLiteral("视频分辨率"),
        new QLabel(video_width_ > 0
            ? QStringLiteral("%1 × %2").arg(video_width_).arg(video_height_)
            : QStringLiteral("尚未取得首帧"), performance_page));
    performance_form->addRow(QStringLiteral("说明"),
        new QLabel(QStringLiteral("解码器和硬件加速由 Core/FFmpeg 根据媒体与设备自动选择。"), performance_page));
    add_settings_page(QStringLiteral("性能"), performance_page);

    navigation->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* defaults = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    root->addWidget(buttons);

    connect(defaults, &QPushButton::clicked, &dialog, [=]
    {
        remember_position->setChecked(true);
        auto_play_next->setChecked(true);
        default_playback_rate->setValue(1.0);
        repeat_mode->setCurrentIndex(0);
        shuffle_enabled->setChecked(false);
        hide_delay->setValue(2);
        osd_duration->setValue(1500);
        osd_position->setCurrentIndex(1);
        show_playlist->setChecked(true);
        always_on_top->setChecked(false);
        middle_click->setCurrentIndex(1);
        double_click->setCurrentIndex(0);
        wheel_action->setCurrentIndex(0);
        mouse_gestures->setChecked(false);
        aspect->setCurrentIndex(0);
        custom_aspect->setValue(16.0 / 9.0);
        brightness->setValue(0.0);
        contrast->setValue(1.0);
        saturation->setValue(1.0);
        hue->setValue(0);
        sharpness->setValue(0.0);
        denoise->setValue(0.0);
        grayscale->setChecked(false);
        subtitle_enabled->setChecked(true);
        auto_match_subtitle->setChecked(true);
        subtitle_offset->setValue(0);
        subtitle_font->setCurrentFont(QFont(QStringLiteral("Segoe UI")));
        subtitle_size->setValue(0);
        subtitle_color->setCurrentIndex(0);
        subtitle_position->setValue(80);
        screenshot_directory->setText(QDir(application_data_directory()).filePath(
            QStringLiteral("Screenshots")));
        screenshot_format->setCurrentIndex(0);
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
    const bool fullscreen_before_dialog = isFullScreen();
    if (fullscreen_before_dialog)
        fullscreen_hide_timer_->stop();
    const int dialog_result = dialog.exec();
    if (fullscreen_before_dialog && isFullScreen())
        showFullscreenControls();
    if (dialog_result != QDialog::Accepted)
        return;

    remember_position_ = remember_position->isChecked();
    auto_play_next_ = auto_play_next->isChecked();
    repeat_mode_ = repeat_mode->currentData().toInt();
    shuffle_enabled_ = shuffle_enabled->isChecked();
    wheel_action_ = wheel_action->currentData().toInt();
    if (std::abs(playback_rate_ - default_playback_rate->value()) > 0.001)
        setPlaybackRate(static_cast<float>(default_playback_rate->value()));
    fullscreen_hide_delay_ms_ = hide_delay->value() * 1000;
    osd_duration_ms_ = osd_duration->value();
    osd_position_ = osd_position->currentData().toInt();
    fullscreen_hide_timer_->setInterval(fullscreen_hide_delay_ms_);
    playlist_panel_visible_ = show_playlist->isChecked();
    always_on_top_ = always_on_top->isChecked();
    middle_click_action_ = middle_click->currentData().toInt();
    double_click_action_ = double_click->currentData().toInt();
    mouse_gestures_enabled_ = mouse_gestures->isChecked();
    if (video_surface_)
        video_surface_->set_gesture_enabled(mouse_gestures_enabled_);
    if (shuffle_button_)
    {
        shuffle_button_->setChecked(shuffle_enabled_);
        shuffle_button_->setText(shuffle_enabled_
            ? QStringLiteral("随机：开") : QStringLiteral("随机：关"));
    }
    if (repeat_button_)
        repeat_button_->setText(repeat_mode_ == 0 ? QStringLiteral("循环：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环：列表") : QStringLiteral("循环：单项"));
    resetShuffleOrder();
    saveVideoPlaylist();
    if (playlist_visibility_action_)
        playlist_visibility_action_->setChecked(playlist_panel_visible_);
    if (always_on_top_action_)
        always_on_top_action_->setChecked(always_on_top_);
    if (playlist_panel_)
        playlist_panel_->setVisible(playlist_panel_visible_);
    const bool was_fullscreen = isFullScreen();
    const bool was_maximized = isMaximized();
    setWindowFlag(Qt::WindowStaysOnTopHint, always_on_top_);
    if (was_fullscreen)
        showFullScreen();
    else if (was_maximized)
        showMaximized();
    else
        show();

    const int selected_aspect = aspect->currentData().toInt();
    video_stretch_ = selected_aspect == 3;
    video_aspect_ratio_override_ = selected_aspect == 1 ? 16.0f / 9.0f
        : selected_aspect == 2 ? 4.0f / 3.0f
        : selected_aspect == 4 ? static_cast<float>(custom_aspect->value())
        : 0.0f;
    video_brightness_ = static_cast<float>(brightness->value());
    video_contrast_ = static_cast<float>(contrast->value());
    video_saturation_ = static_cast<float>(saturation->value());
    video_hue_degrees_ = hue->value();
    video_sharpness_ = static_cast<float>(sharpness->value());
    video_denoise_ = static_cast<float>(denoise->value());
    video_grayscale_ = grayscale->isChecked();
    subtitles_enabled_ = subtitle_enabled->isChecked();
    auto_match_subtitle_ = auto_match_subtitle->isChecked();
    subtitle_offset_ms_ = subtitle_offset->value();
    subtitle_font_family_ = subtitle_font->currentFont().family();
    subtitle_font_size_ = subtitle_size->value();
    subtitle_color_ = subtitle_color->currentData().toString();
    subtitle_vertical_position_ = subtitle_position->value();
    screenshot_directory_ = screenshot_directory->text().trimmed().isEmpty()
        ? QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))
        : screenshot_directory->text().trimmed();
    screenshot_format_ = screenshot_format->currentData().toString();
    video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
    video_surface_->set_stretch(video_stretch_);
    video_surface_->set_video_filter(
        video_brightness_, video_contrast_, video_saturation_,
        video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);

    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/Playback/RememberPosition"), remember_position_);
    settings.setValue(QStringLiteral("Video/Playback/AutoPlayNext"), auto_play_next_);
    settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), always_on_top_);
    settings.setValue(QStringLiteral("Video/UI/FullscreenHideDelayMs"), fullscreen_hide_delay_ms_);
    settings.setValue(QStringLiteral("Video/UI/OSDDurationMs"), osd_duration_ms_);
    settings.setValue(QStringLiteral("Video/UI/OSDPosition"), osd_position_);
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it)
    {
        const QKeySequence sequence = shortcut_edits.value(it.key())->keySequence();
        it.value()->setKey(sequence);
        settings.setValue(
            QStringLiteral("Video/Shortcuts/") + it.key(),
            sequence.toString(QKeySequence::PortableText));
    }
    settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), playlist_panel_visible_);
    settings.setValue(QStringLiteral("Video/Mouse/MiddleClickAction"), middle_click_action_);
    settings.setValue(QStringLiteral("Video/Mouse/DoubleClickAction"), double_click_action_);
    settings.setValue(QStringLiteral("Video/Mouse/WheelAction"), wheel_action_);
    settings.setValue(QStringLiteral("Video/Mouse/RightDragGestures"), mouse_gestures_enabled_);
    settings.setValue(QStringLiteral("Video/Display/AspectRatio"), video_aspect_ratio_override_);
    settings.setValue(QStringLiteral("Video/Display/Stretch"), video_stretch_);
    settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
    settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
    settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
    settings.setValue(QStringLiteral("Video/Filter/HueDegrees"), video_hue_degrees_);
    settings.setValue(QStringLiteral("Video/Filter/Sharpness"), video_sharpness_);
    settings.setValue(QStringLiteral("Video/Filter/Denoise"), video_denoise_);
    settings.setValue(QStringLiteral("Video/Filter/Grayscale"), video_grayscale_);
    settings.setValue(QStringLiteral("Video/Subtitle/Enabled"), subtitles_enabled_);
    settings.setValue(QStringLiteral("Video/Subtitle/AutoMatch"), auto_match_subtitle_);
    settings.setValue(QStringLiteral("Video/Subtitle/OffsetMs"), subtitle_offset_ms_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontFamily"), subtitle_font_family_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontSize"), subtitle_font_size_);
    settings.setValue(QStringLiteral("Video/Subtitle/Color"), subtitle_color_);
    settings.setValue(QStringLiteral("Video/Subtitle/VerticalPosition"), subtitle_vertical_position_);
    settings.setValue(QStringLiteral("Video/Screenshot/Directory"), screenshot_directory_);
    settings.setValue(QStringLiteral("Video/Screenshot/Format"), output_format);
    settings.sync();

    setSubtitleOffset(subtitle_offset_ms_);
    postCommand([this, enabled = subtitles_enabled_]
    {
        if (player_)
            lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
    });
    pending_video_frame_valid_ = false;
    requestVideoFrame();
    if (isFullScreen())
    {
        showFullscreenControls();
        if (fullscreen_hide_delay_ms_ == 0)
        {
            control_panel_->show();
            menuBar()->show();
        }
    }
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

void MainWindow::refreshLibrary()
{
    lnplayer_t* player_handle = player_.load(std::memory_order_acquire);
    if (!player_handle)
        return;

    const QByteArray query_utf8 = library_search_->text().toUtf8();
    library_list_->clear();

    postLibrary([this, player_handle, query_utf8]
    {
        struct TrackItem
        {
            QString label;
            QString path;
        };
        struct SearchContext
        {
            std::vector<TrackItem> tracks;
        } context;

        const lnplayer_result_t result = lnplayer_library_search_tracks(
            player_handle,
            query_utf8.constData(),
            500,
            +[](const lnplayer_library_track_t* track, void* user_data)
            {
                auto* context = static_cast<SearchContext*>(user_data);
                const QString title = QString::fromUtf8(track->title_utf8);
                const QString artist = QString::fromUtf8(track->artist_utf8);
                context->tracks.push_back({
                    artist.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, artist),
                    QString::fromUtf8(track->path_utf8)
                });
            },
            &context);

        QMetaObject::invokeMethod(this,
            [this, result, tracks = std::move(context.tracks)]
        {
            for (const TrackItem& track : tracks)
            {
                auto* item = new QListWidgetItem(track.label, library_list_);
                item->setData(Qt::UserRole, track.path);
            }
            if (result != LNPLAYER_RESULT_OK)
                status_label_->setText(QStringLiteral("音乐库查询失败"));
        }, Qt::QueuedConnection);
    });
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
                video_width_ = hardware_owner ? hardware_width : image.width();
                video_height_ = hardware_owner ? hardware_height : image.height();
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
            QImage overlay(overlay_size, QImage::Format_ARGB32);
            overlay.fill(Qt::transparent);
            if (render_subtitles && !embedded_subtitle_bitmap.isNull())
                draw_subtitle_bitmap(overlay, embedded_subtitle_bitmap, true, subtitle_aspect);
            draw_subtitle_text(
                overlay, subtitle, font_family, subtitle_color, font_size, vertical_position);

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
    constexpr int maximum_history_frames = 120;
    if (video_frame_history_.size() > maximum_history_frames)
        video_frame_history_.removeFirst();
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
                postCommand([this, frame = std::move(frame), generation, media_path]
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
                    }, Qt::QueuedConnection);
                });
            }
            else
            {
                video_surface_->set_frame(frame);
                video_frame_pending_ = false;
            }
        }, Qt::QueuedConnection);
    });
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
    video_timer_->start();
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

void MainWindow::takeScreenshot()
{
    QImage screenshot = video_surface_ ? video_surface_->capture_frame() : QImage();
    if (screenshot.isNull())
        screenshot = last_video_frame_;
    if (screenshot.isNull())
        return;

    if (screenshot_directory_.isEmpty())
        screenshot_directory_ = QDir(application_data_directory()).filePath(
            QStringLiteral("Screenshots"));
    QString selected_filter = screenshot_format_ == QStringLiteral("jpg")
        ? QStringLiteral("JPEG 图片 (*.jpg *.jpeg)")
        : screenshot_format_ == QStringLiteral("bmp")
            ? QStringLiteral("BMP 图片 (*.bmp)")
            : QStringLiteral("PNG 图片 (*.png)");
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存视频截图"),
        QDir(screenshot_directory_).filePath(
            QStringLiteral("screenshot.%1").arg(screenshot_format_)),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"),
        &selected_filter,
        QFileDialog::Options());
    if (path.isEmpty())
        return;

    if (selected_filter.startsWith(QStringLiteral("JPEG")))
        screenshot_format_ = QStringLiteral("jpg");
    else if (selected_filter.startsWith(QStringLiteral("BMP")))
        screenshot_format_ = QStringLiteral("bmp");
    else
        screenshot_format_ = QStringLiteral("png");

    QString output_path = path;
    const QString suffix = QFileInfo(output_path).suffix().toLower();
    if (suffix.isEmpty())
        output_path += QStringLiteral(".") + screenshot_format_;
    else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        screenshot_format_ = QStringLiteral("jpg");
    else if (suffix == QStringLiteral("bmp"))
        screenshot_format_ = QStringLiteral("bmp");
    else if (suffix == QStringLiteral("png"))
        screenshot_format_ = QStringLiteral("png");

    const QString output_format = screenshot_format_;
    const QByteArray image_format = output_format.toLatin1();
    postBackground([this, screenshot = std::move(screenshot), output_path, output_format, image_format]() mutable
    {
        QDir().mkpath(QFileInfo(output_path).absolutePath());
        const bool saved = screenshot.save(output_path, image_format.constData());
        QMetaObject::invokeMethod(this, [this, saved, output_path, output_format]
        {
            if (!saved)
            {
                statusBar()->showMessage(QStringLiteral("截图保存失败"));
                return;
            }

            screenshot_directory_ = QFileInfo(output_path).absolutePath();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/Screenshot/Directory"), screenshot_directory_);
            settings.setValue(QStringLiteral("Video/Screenshot/Format"), output_format);
            settings.sync();
            statusBar()->showMessage(QStringLiteral("截图已保存：%1").arg(output_path));
            showOsd(QStringLiteral("截图已保存"));
        }, Qt::QueuedConnection);
    });
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
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/PlaybackRate"), rate);
    settings.sync();

    postCommand([this, rate]
    {
        if (player_)
            lnplayer_set_playback_rate(player_, rate);
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

        postCommand([this, entry, overlay = std::move(overlay)]() mutable
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
        });
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

    postCommand([this]
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
