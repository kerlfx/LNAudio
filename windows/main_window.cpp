#include "main_window.h"

#include <algorithm>

#include <QByteArray>
#include <QFont>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMetaObject>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
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

MainWindow::MainWindow(PlayerMode mode, const QString& initial_media, QWidget* parent)
    : QMainWindow(parent)
    , mode_(mode)
    , status_label_(new QLabel(this))
    , position_label_(new QLabel(QStringLiteral("00:00 / --:--"), this))
    , media_title_label_(new QLabel(QStringLiteral("未打开媒体"), this))
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
    if (mode_ == PlayerMode::Audio)
    {
        auto* import_action = file_menu->addAction(QStringLiteral("导入音乐目录..."));
        connect(import_action, &QAction::triggered, this, [this]
        {
            const QString directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择音乐目录"));
            if (directory.isEmpty())
                return;

            const QByteArray path_utf8 = directory.toUtf8();
            status_label_->setText(QStringLiteral("正在扫描音乐目录..."));
            QMetaObject::invokeMethod(library_worker_, [this, path_utf8]
            {
                int imported_count = 0;
                const lnplayer_result_t result = player_
                    ? lnplayer_library_scan_directory(
                        player_, path_utf8.constData(), 1, &imported_count)
                    : LNPLAYER_RESULT_INVALID_HANDLE;

                QMetaObject::invokeMethod(this, [this, result, imported_count]
                {
                    if (result == LNPLAYER_RESULT_OK)
                    {
                        status_label_->setText(
                            QStringLiteral("目录扫描完成，导入 %1 个音频文件").arg(imported_count));
                        statusBar()->showMessage(QStringLiteral("音乐库已更新"));
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
        auto* video_surface = new QWidget(content);
        video_surface->setMinimumSize(640, 360);
        video_surface->setStyleSheet(QStringLiteral("background-color: #101010;"));
        layout->addWidget(video_surface, 1);
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
    control_layout->addStretch(1);
    control_layout->addWidget(new QLabel(QStringLiteral("音量"), content));
    control_layout->addWidget(volume_slider_);
    layout->addLayout(control_layout);

    layout->addWidget(status_label_);
    setCentralWidget(content);
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
                    message);
            }, Qt::QueuedConnection);
        };
        config.user_data = this;

        const lnplayer_result_t result = lnplayer_create(&config, &player_);
        QMetaObject::invokeMethod(this, [this, result]
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                status_label_->setText(QStringLiteral("播放器已就绪"));
                statusBar()->showMessage(QStringLiteral("就绪"));
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
        if (duration_ms_ <= 0)
            return;

        const int64_t target = duration_ms_ * seek_slider_->value() / 1000;
        QMetaObject::invokeMethod(command_worker_, [this, target]
        {
            if (player_)
            {
                uint64_t operation_id = 0;
                lnplayer_seek(player_, target, &operation_id);
            }
        }, Qt::QueuedConnection);
    });

    snapshot_timer_->setInterval(250);
    connect(snapshot_timer_, &QTimer::timeout, this, &MainWindow::refreshSnapshot);

    if (!initial_media.isEmpty())
        openFiles({QUrl::fromLocalFile(initial_media)});
}

MainWindow::~MainWindow()
{
    snapshot_timer_->stop();

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
    if (urls.isEmpty())
        return;

    const QString source = urls.first().toLocalFile();
    if (source.isEmpty())
        return;

    media_title_label_->setText(QFileInfo(source).fileName());
    status_label_->setText(QStringLiteral("正在打开：%1").arg(source));
    statusBar()->showMessage(QStringLiteral("正在打开媒体"));

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
                status_label_->setText(QStringLiteral("媒体打开请求失败"));
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void MainWindow::handleCoreEvent(
    lnplayer_event_type_t type,
    lnplayer_result_t result,
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms,
    const QString& message)
{
    updatePlaybackState(state, position_ms, duration_ms);

    switch (type)
    {
    case LNPLAYER_EVENT_OPEN_COMPLETED:
        if (result == LNPLAYER_RESULT_OK)
        {
            status_label_->setText(QStringLiteral("媒体已打开"));
            statusBar()->showMessage(QStringLiteral("就绪"));
            seek_slider_->setEnabled(duration_ms > 0);
            snapshot_timer_->start();
        }
        else
        {
            status_label_->setText(message.isEmpty()
                ? QStringLiteral("媒体打开失败")
                : message);
            statusBar()->showMessage(QStringLiteral("打开失败"));
        }
        break;

    case LNPLAYER_EVENT_PLAYBACK_ENDED:
        status_label_->setText(QStringLiteral("播放结束"));
        break;

    case LNPLAYER_EVENT_ERROR:
        status_label_->setText(message.isEmpty()
            ? QStringLiteral("播放发生错误")
            : message);
        break;

    case LNPLAYER_EVENT_STATE_CHANGED:
    case LNPLAYER_EVENT_SEEK_COMPLETED:
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

        QMetaObject::invokeMethod(this, [this, state, position_ms, duration_ms]
        {
            updatePlaybackState(state, position_ms, duration_ms);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MainWindow::updatePlaybackState(
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms)
{
    duration_ms_ = duration_ms;

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
