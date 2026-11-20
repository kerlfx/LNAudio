#include "main_window.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(PlayerMode mode, QWidget* parent)
    : QMainWindow(parent)
    , mode_(mode)
    , status_label_(new QLabel(this))
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
        status_label_->setText(QStringLiteral("视频输出尚未接入"));
    }
    else
    {
        auto* audio_view = new QLabel(QStringLiteral("LNPlayer 音频播放器"), content);
        audio_view->setAlignment(Qt::AlignCenter);
        layout->addWidget(audio_view, 1);
        status_label_->setText(QStringLiteral("音频输出尚未接入"));
    }

    layout->addWidget(status_label_);
    setCentralWidget(content);
    statusBar()->showMessage(QStringLiteral("就绪"));
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
        QStringLiteral("打开媒体文件"));

    QList<QUrl> urls;
    for (const QString& file : files)
        urls.append(QUrl::fromLocalFile(file));

    openFiles(urls);
}

void MainWindow::openFiles(const QList<QUrl>& urls)
{
    if (urls.isEmpty())
        return;

    status_label_->setText(urls.first().toLocalFile());
    statusBar()->showMessage(QStringLiteral("已选择媒体文件；Core 播放接口尚未接入"));
}
