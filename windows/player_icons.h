#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPixmap>
#include <QRectF>
#include <QString>

namespace PlayerIcons
{

enum class Name
{
    Play, Pause, Stop, Previous, Next,
    Volume, VolumeMuted, AudioTrack, Subtitles,
    Minimize, Maximize, Restore, Close,
    Add, Remove, Clear, MoveUp, MoveDown,
    Shuffle, Repeat, RepeatOne, Search,
    Settings, General, Playback, Interface, Video, Filters, Keyboard, Screenshot, Performance
};

inline QIcon make(Name name, const QColor& color = QColor(QStringLiteral("#d7dde7")))
{
    constexpr qreal logical_size = 24.0;
    constexpr qreal pixel_ratio = 2.0;
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(pixel_ratio);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(logical_size / 24.0, logical_size / 24.0);
    QPen pen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    switch (name)
    {
    case Name::Play:
        path.moveTo(8, 5); path.lineTo(19, 12); path.lineTo(8, 19); path.closeSubpath();
        painter.setBrush(color); painter.drawPath(path); break;
    case Name::Pause:
        painter.drawRoundedRect(QRectF(6.5, 5, 4, 14), 1, 1);
        painter.drawRoundedRect(QRectF(13.5, 5, 4, 14), 1, 1); break;
    case Name::Stop:
        painter.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5); break;
    case Name::Previous:
        painter.drawLine(6, 5, 6, 19);
        path.moveTo(18, 5); path.lineTo(9, 12); path.lineTo(18, 19); painter.drawPath(path); break;
    case Name::Next:
        painter.drawLine(18, 5, 18, 19);
        path.moveTo(6, 5); path.lineTo(15, 12); path.lineTo(6, 19); painter.drawPath(path); break;
    case Name::Volume:
        path.moveTo(4, 10); path.lineTo(8, 10); path.lineTo(13, 6); path.lineTo(13, 18);
        path.lineTo(8, 14); path.lineTo(4, 14); path.closeSubpath();
        painter.setBrush(color); painter.drawPath(path);
        path = QPainterPath(); path.arcMoveTo(QRectF(9, 5, 12, 14), -55);
        path.arcTo(QRectF(9, 5, 12, 14), -55, 110); painter.drawPath(path);
        path = QPainterPath(); path.arcMoveTo(QRectF(9, 2, 16, 20), -48);
        path.arcTo(QRectF(9, 2, 16, 20), -48, 96); painter.drawPath(path); break;
    case Name::VolumeMuted:
        path.moveTo(4, 10); path.lineTo(8, 10); path.lineTo(13, 6); path.lineTo(13, 18);
        path.lineTo(8, 14); path.lineTo(4, 14); path.closeSubpath();
        painter.setBrush(color); painter.drawPath(path);
        painter.drawLine(16, 9, 21, 15); painter.drawLine(21, 9, 16, 15); break;
    case Name::AudioTrack:
        painter.drawLine(8, 5, 8, 17); painter.drawLine(8, 5, 18, 3);
        painter.drawLine(18, 3, 18, 15);
        painter.drawEllipse(QRectF(4.5, 15, 7, 5));
        painter.drawEllipse(QRectF(14.5, 13, 7, 5)); break;
    case Name::Subtitles:
        painter.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        painter.drawLine(6, 10, 11, 10); painter.drawLine(13, 10, 18, 10);
        painter.drawLine(6, 14, 10, 14); painter.drawLine(12, 14, 18, 14); break;
    case Name::Minimize:
        painter.drawLine(5, 17, 19, 17); break;
    case Name::Maximize:
        painter.drawRect(QRectF(5, 5, 14, 14)); break;
    case Name::Restore:
        painter.drawRect(QRectF(7, 5, 12, 12));
        painter.drawLine(5, 9, 5, 19); painter.drawLine(5, 19, 15, 19); break;
    case Name::Close:
        painter.drawLine(6, 6, 18, 18); painter.drawLine(18, 6, 6, 18); break;
    case Name::Add:
        painter.drawLine(12, 5, 12, 19); painter.drawLine(5, 12, 19, 12); break;
    case Name::Remove:
        painter.drawLine(5, 12, 19, 12); break;
    case Name::Clear:
        painter.drawLine(5, 7, 19, 7); painter.drawLine(9, 7, 9, 5);
        painter.drawLine(15, 7, 15, 5);
        painter.drawLine(7, 8, 8, 19); painter.drawLine(17, 8, 16, 19);
        painter.drawLine(8, 19, 16, 19); painter.drawLine(10, 10, 10, 17);
        painter.drawLine(14, 10, 14, 17); break;
    case Name::MoveUp:
        painter.drawLine(12, 19, 12, 6); painter.drawLine(6, 12, 12, 6);
        painter.drawLine(12, 6, 18, 12); break;
    case Name::MoveDown:
        painter.drawLine(12, 5, 12, 18); painter.drawLine(6, 12, 12, 18);
        painter.drawLine(12, 18, 18, 12); break;
    case Name::Shuffle:
        path.moveTo(4, 7); path.lineTo(7, 7); path.lineTo(17, 17); path.lineTo(20, 17);
        path.moveTo(17, 5); path.lineTo(20, 5); path.lineTo(20, 8);
        path.moveTo(4, 17); path.lineTo(7, 17); path.lineTo(17, 7);
        path.moveTo(17, 19); path.lineTo(20, 19); path.lineTo(20, 16); painter.drawPath(path); break;
    case Name::Repeat:
        path.moveTo(18, 7); path.lineTo(21, 10); path.lineTo(18, 13);
        path.moveTo(21, 10); path.lineTo(6, 10); path.lineTo(6, 16);
        path.moveTo(6, 17); path.lineTo(3, 14); path.lineTo(6, 11);
        path.moveTo(3, 14); path.lineTo(18, 14); path.lineTo(18, 8); painter.drawPath(path); break;
    case Name::RepeatOne:
        path.moveTo(18, 7); path.lineTo(21, 10); path.lineTo(18, 13);
        path.moveTo(21, 10); path.lineTo(6, 10); path.lineTo(6, 16);
        path.moveTo(6, 17); path.lineTo(3, 14); path.lineTo(6, 11);
        path.moveTo(3, 14); path.lineTo(18, 14); path.lineTo(18, 8); painter.drawPath(path);
        painter.drawText(QRectF(8, 10, 8, 7), Qt::AlignCenter, QStringLiteral("1")); break;
    case Name::Search:
        painter.drawEllipse(QRectF(4, 4, 11, 11)); painter.drawLine(13, 13, 20, 20); break;
    case Name::Settings:
        painter.drawEllipse(QRectF(7, 7, 10, 10));
        painter.drawEllipse(QRectF(10, 10, 4, 4));
        painter.drawLine(12, 2, 12, 5); painter.drawLine(12, 19, 12, 22);
        painter.drawLine(2, 12, 5, 12); painter.drawLine(19, 12, 22, 12);
        painter.drawLine(5, 5, 7, 7); painter.drawLine(17, 17, 19, 19);
        painter.drawLine(5, 19, 7, 17); painter.drawLine(17, 7, 19, 5); break;
    case Name::General:
        painter.drawEllipse(QPointF(12, 12), 8, 8);
        painter.drawLine(12, 4, 12, 8); painter.drawLine(12, 16, 12, 20);
        painter.drawLine(4, 12, 8, 12); painter.drawLine(16, 12, 20, 12); break;
    case Name::Playback:
        path.moveTo(8, 5); path.lineTo(19, 12); path.lineTo(8, 19); path.closeSubpath();
        painter.setBrush(color); painter.drawPath(path); break;
    case Name::Interface:
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        painter.drawLine(3, 9, 21, 9); painter.drawLine(8, 9, 8, 20); break;
    case Name::Video:
        painter.drawRoundedRect(QRectF(3, 6, 18, 12), 2, 2);
        painter.drawLine(8, 3, 16, 3); painter.drawLine(12, 3, 12, 6);
        painter.drawLine(8, 18, 6, 21); painter.drawLine(16, 18, 18, 21); break;
    case Name::Filters:
        painter.drawLine(4, 6, 20, 6); painter.drawLine(4, 12, 20, 12);
        painter.drawLine(4, 18, 20, 18);
        painter.drawEllipse(QRectF(8, 4, 4, 4)); painter.drawEllipse(QRectF(14, 10, 4, 4));
        painter.drawEllipse(QRectF(6, 16, 4, 4)); break;
    case Name::Keyboard:
        painter.drawRoundedRect(QRectF(3, 6, 18, 12), 2, 2);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 5; ++column)
                painter.drawRect(QRectF(5 + column * 3, 8 + row * 4, 1.5, 1.5));
        painter.drawLine(8, 16, 16, 16); break;
    case Name::Screenshot:
        painter.drawRoundedRect(QRectF(3, 7, 18, 13), 2, 2);
        painter.drawRoundedRect(QRectF(8, 4, 8, 4), 1, 1);
        painter.drawEllipse(QRectF(8, 10, 8, 8)); break;
    case Name::Performance:
        path.arcMoveTo(QRectF(4, 4, 16, 16), 180);
        path.arcTo(QRectF(4, 4, 16, 16), 180, -180); painter.drawPath(path);
        painter.drawLine(12, 12, 17, 8); painter.drawLine(5, 18, 19, 18); break;
    }
    return QIcon(pixmap);
}

} // namespace PlayerIcons
