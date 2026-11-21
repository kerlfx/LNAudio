#pragma once

#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QSvgRenderer>

namespace PlayerIcons
{

enum class Name
{
    Play, Pause, Stop, Previous, Next,
    Volume, VolumeMuted, AudioTrack, Subtitles,
    Minimize, Maximize, Restore, Close,
    Add, Remove, Clear, MoveUp, MoveDown,
    Shuffle, Repeat, RepeatOne, Search,
    Settings, General, Playback, Interface, Video, Filters, Keyboard, Screenshot, Performance,
    Folder, External, Reset, Playlist
};

inline const char* resource_id(Name name)
{
    switch (name)
    {
    case Name::Play: return "play";
    case Name::Pause: return "pause";
    case Name::Stop: return "stop";
    case Name::Previous: return "previous";
    case Name::Next: return "next";
    case Name::Volume: return "volume";
    case Name::VolumeMuted: return "volume_muted";
    case Name::AudioTrack: return "audio_track";
    case Name::Subtitles: return "subtitles";
    case Name::Minimize: return "minimize";
    case Name::Maximize: return "maximize";
    case Name::Restore: return "restore";
    case Name::Close: return "close";
    case Name::Add: return "add";
    case Name::Remove: return "remove";
    case Name::Clear: return "clear";
    case Name::MoveUp: return "move_up";
    case Name::MoveDown: return "move_down";
    case Name::Shuffle: return "shuffle";
    case Name::Repeat: return "repeat";
    case Name::RepeatOne: return "repeat_one";
    case Name::Search: return "search";
    case Name::Settings: return "settings";
    case Name::General: return "general";
    case Name::Playback: return "playback";
    case Name::Interface: return "interface";
    case Name::Video: return "video";
    case Name::Filters: return "filters";
    case Name::Keyboard: return "keyboard";
    case Name::Screenshot: return "screenshot";
    case Name::Performance: return "performance";
    case Name::Folder: return "folder";
    case Name::External: return "external";
    case Name::Reset: return "reset";
    case Name::Playlist: return "playlist";
    }
    return "play";
}

inline const QByteArray& svg_data()
{
    static const QByteArray data = []
    {
        QFile file(QStringLiteral(":/icons/player_icons.svg"));
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        return file.readAll();
    }();
    return data;
}

inline QIcon make(Name name, const QColor& color = QColor(QStringLiteral("#f0f4fa")))
{
    constexpr qreal logical_size = 24.0;
    constexpr qreal pixel_ratio = 2.0;

    QByteArray data = svg_data();
    data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(data);
    if (!renderer.isValid())
        return {};

    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(pixel_ratio);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QString::fromLatin1(resource_id(name)),
                    QRectF(0, 0, logical_size, logical_size));
    painter.end();

    return QIcon(pixmap);
}

} // namespace PlayerIcons
