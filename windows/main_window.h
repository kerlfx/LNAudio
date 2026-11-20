#ifndef LNPLAYER_MAIN_WINDOW_H
#define LNPLAYER_MAIN_WINDOW_H

#include <QMainWindow>
#include <QString>
#include <QList>
#include <QUrl>

#include "lnplayer.h"

class QLabel;
class QObject;
class QPushButton;
class QSlider;
class QThread;
class QTimer;
class QWidget;

enum class PlayerMode
{
    Audio,
    Video
};

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(PlayerMode mode, const QString& initial_media = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void openFile();
    void openFiles(const QList<QUrl>& urls);
    void handleCoreEvent(
        lnplayer_event_type_t type,
        lnplayer_result_t result,
        lnplayer_state_t state,
        int64_t position_ms,
        int64_t duration_ms,
        const QString& message);
    void refreshSnapshot();
    void updatePlaybackState(lnplayer_state_t state, int64_t position_ms, int64_t duration_ms);

    PlayerMode mode_;
    QLabel* status_label_;
    QLabel* position_label_;
    QLabel* media_title_label_;
    QSlider* seek_slider_;
    QSlider* volume_slider_;
    QPushButton* play_button_;
    QPushButton* pause_button_;
    QPushButton* stop_button_;
    QObject* command_worker_;
    QThread* command_thread_;
    QTimer* snapshot_timer_;
    int64_t duration_ms_ = -1;
    bool slider_dragging_ = false;
};

#endif
