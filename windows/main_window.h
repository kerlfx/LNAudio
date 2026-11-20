#ifndef LNPLAYER_MAIN_WINDOW_H
#define LNPLAYER_MAIN_WINDOW_H

#include <QMainWindow>
#include <QList>
#include <QUrl>

class QLabel;
class QWidget;

enum class PlayerMode
{
    Audio,
    Video
};

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(PlayerMode mode, QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void openFile();
    void openFiles(const QList<QUrl>& urls);

    PlayerMode mode_;
    QLabel* status_label_;
};

#endif
