#ifndef LNPLAYER_MAIN_WINDOW_H
#define LNPLAYER_MAIN_WINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QElapsedTimer>
#include <QString>
#include <QList>
#include <QUrl>

#include "lnplayer.h"

class QLabel;
class VideoSurface;
class QLineEdit;
class QListWidget;
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
    void refreshLibrary();
    void requestVideoFrame();
    void presentPendingVideoFrame();
    void togglePlayback();
    void seekRelative(int64_t offset_ms);
    void takeScreenshot();
    void stepVideoFrame();
    void setPlaybackRate(float rate);
    void handleCoreEvent(
        lnplayer_event_type_t type,
        lnplayer_result_t result,
        lnplayer_state_t state,
        int64_t position_ms,
        int64_t duration_ms,
        uint64_t generation,
        const QString& message);
    void refreshSnapshot();
    void updatePlaybackState(lnplayer_state_t state, int64_t position_ms, int64_t duration_ms);

    PlayerMode mode_;
    QLabel* status_label_;
    QLabel* position_label_;
    QLabel* media_title_label_;
    QLineEdit* library_search_;
    QListWidget* library_list_;
    VideoSurface* video_surface_ = nullptr;
    QSlider* seek_slider_;
    QSlider* volume_slider_;
    QPushButton* play_button_;
    QPushButton* pause_button_;
    QPushButton* stop_button_;
    QObject* command_worker_;
    QThread* command_thread_;
    QObject* library_worker_;
    QThread* library_thread_;
    QTimer* snapshot_timer_;
    QTimer* video_timer_;
    lnplayer_t* player_ = nullptr;
    int64_t duration_ms_ = -1;
    bool slider_dragging_ = false;
    bool has_video_ = false;
    bool has_audio_ = false;
    bool is_playing_ = false;
    bool video_frame_pending_ = false;
    bool pending_video_frame_valid_ = false;
    bool video_first_frame_received_ = false;
    bool video_end_of_stream_ = false;
    QImage pending_video_frame_;
    QImage last_video_frame_;
    int64_t pending_video_pts_ms_ = 0;
    int64_t current_position_ms_ = 0;
    int64_t video_clock_base_pts_ms_ = 0;
    QElapsedTimer video_clock_;
    int muted_volume_ = 1000;
    float playback_rate_ = 1.0f;
    uint64_t media_generation_ = 0;
};

#endif
