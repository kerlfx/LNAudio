#ifndef LNPLAYER_MAIN_WINDOW_H
#define LNPLAYER_MAIN_WINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QString>
#include <QList>
#include <QUrl>

#include <vector>
#include <memory>
#include <cstdint>

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
    struct SubtitleCue
    {
        int64_t start_ms = 0;
        int64_t end_ms = 0;
        QString text;
    };

    void openFile();
    void loadExternalSubtitle();
    QString subtitleAt(int64_t position_ms) const;
    void openFiles(const QList<QUrl>& urls);
    void appendPlaylistFiles(const QList<QUrl>& urls);
    void rebuildPlaylistWidget();
    void removeSelectedPlaylistItem();
    void moveSelectedPlaylistItem(int offset);
    void cycleRepeatMode();
    void resetShuffleOrder();
    int nextPlaylistIndex(bool manual) const;
    void openPlaylistIndex(int index);
    void playPrevious();
    void playNext();
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
    QListWidget* playlist_widget_ = nullptr;
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
    void* video_device_identity_ = nullptr;
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
    std::shared_ptr<void> pending_hardware_frame_owner_;
    void* pending_hardware_texture_ = nullptr;
    uint32_t pending_hardware_array_slice_ = 0;
    uint32_t pending_hardware_format_ = 0;
    int pending_hardware_width_ = 0;
    int pending_hardware_height_ = 0;
    QImage last_video_frame_;
    int64_t pending_video_pts_ms_ = 0;
    uint64_t pending_video_generation_ = 0;
    int64_t current_position_ms_ = 0;
    int muted_volume_ = 1000;
    float playback_rate_ = 1.0f;
    uint64_t media_generation_ = 0;
    std::vector<SubtitleCue> subtitle_cues_;
    QList<QUrl> playlist_;
    int playlist_index_ = -1;
    bool playlist_open_pending_ = false;
    bool subtitles_enabled_ = true;
    int64_t subtitle_offset_ms_ = 0;
    bool shuffle_enabled_ = false;
    int repeat_mode_ = 0;
    QList<int> shuffle_order_;
};

#endif
