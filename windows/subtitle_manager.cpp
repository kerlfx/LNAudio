#include "subtitle_manager.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <utility>

bool SubtitleManager::load_file(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("无法打开字幕文件");
        return false;
    }

    const QByteArray subtitle_data = file.readAll();
    QString subtitle_text;
    if (subtitle_data.startsWith("\\xEF\\xBB\\xBF"))
    {
        subtitle_text = QString::fromUtf8(
            subtitle_data.constData() + 3, subtitle_data.size() - 3);
    }
    else if (subtitle_data.startsWith("\\xFF\\xFE\\x00\\x00"))
    {
        QStringDecoder decoder("UTF-32LE");
        subtitle_text = decoder(subtitle_data.mid(4));
        if (decoder.hasError())
        {
            error = QStringLiteral("UTF-32LE 字幕编码无效");
            return false;
        }
    }
    else if (subtitle_data.startsWith("\\x00\\x00\\xFE\\xFF"))
    {
        QStringDecoder decoder("UTF-32BE");
        subtitle_text = decoder(subtitle_data.mid(4));
        if (decoder.hasError())
        {
            error = QStringLiteral("UTF-32BE 字幕编码无效");
            return false;
        }
    }
    else if (subtitle_data.startsWith("\\xFF\\xFE"))
    {
        QStringDecoder decoder("UTF-16LE");
        subtitle_text = decoder(subtitle_data.mid(2));
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }
    else if (subtitle_data.startsWith("\\xFE\\xFF"))
    {
        QStringDecoder decoder("UTF-16BE");
        subtitle_text = decoder(subtitle_data.mid(2));
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }
    else
    {
        QStringDecoder decoder(QStringDecoder::Utf8);
        subtitle_text = decoder(subtitle_data);
        if (decoder.hasError())
            subtitle_text = QString::fromLocal8Bit(subtitle_data);
    }

    const QStringList lines = subtitle_text.split(
        QRegularExpression(QStringLiteral("\\r?\\n")));
    const QRegularExpression srt_timestamp(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})[,.](\\d{1,3})$"));
    const QRegularExpression ass_timestamp(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})\\.(\\d{1,2})$"));

    const auto parse_timestamp = [](const QRegularExpression& expression,
                                    const QString& value,
                                    int64_t& output,
                                    int fraction_digits)
    {
        const auto match = expression.match(value.trimmed());
        if (!match.hasMatch())
            return false;

        const int hours = match.captured(1).toInt();
        const int minutes = match.captured(2).toInt();
        const int seconds = match.captured(3).toInt();
        QString fraction = match.captured(4);
        while (fraction.size() < fraction_digits)
            fraction.append(QLatin1Char('0'));
        const int milliseconds = fraction.left(fraction_digits).toInt() *
            (fraction_digits == 2 ? 10 : 1);
        output = ((static_cast<int64_t>(hours) * 60 + minutes) * 60 + seconds) *
            1000 + milliseconds;
        return true;
    };

    std::vector<Cue> cues;
    const bool ass_format = path.endsWith(QStringLiteral(".ass"), Qt::CaseInsensitive) ||
        path.endsWith(QStringLiteral(".ssa"), Qt::CaseInsensitive);

    if (ass_format)
    {
        QStringList format_fields{
            QStringLiteral("Layer"), QStringLiteral("Start"), QStringLiteral("End"),
            QStringLiteral("Style"), QStringLiteral("Name"), QStringLiteral("MarginL"),
            QStringLiteral("MarginR"), QStringLiteral("MarginV"),
            QStringLiteral("Effect"), QStringLiteral("Text")
        };
        int start_index = format_fields.indexOf(QStringLiteral("Start"), Qt::CaseInsensitive);
        int end_index = format_fields.indexOf(QStringLiteral("End"), Qt::CaseInsensitive);
        int text_index = format_fields.indexOf(QStringLiteral("Text"), Qt::CaseInsensitive);

        for (const QString& line : lines)
        {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("Format:"), Qt::CaseInsensitive))
            {
                format_fields = trimmed.mid(7).split(QLatin1Char(','));
                for (QString& field : format_fields)
                    field = field.trimmed();
                start_index = format_fields.indexOf(QStringLiteral("Start"), Qt::CaseInsensitive);
                end_index = format_fields.indexOf(QStringLiteral("End"), Qt::CaseInsensitive);
                text_index = format_fields.indexOf(QStringLiteral("Text"), Qt::CaseInsensitive);
                continue;
            }
            if (!trimmed.startsWith(QStringLiteral("Dialogue:"), Qt::CaseInsensitive) ||
                start_index < 0 || end_index < 0 || text_index < 0)
                continue;

            const QString payload = trimmed.mid(9).trimmed();
            QStringList fields;
            int cursor = 0;
            for (int field = 0; field < text_index; ++field)
            {
                const int comma = payload.indexOf(QLatin1Char(','), cursor);
                if (comma < 0)
                    break;
                fields.append(payload.mid(cursor, comma - cursor).trimmed());
                cursor = comma + 1;
            }
            fields.append(payload.mid(cursor));
            if (fields.size() <= std::max({start_index, end_index, text_index}))
                continue;

            Cue cue;
            if (!parse_timestamp(ass_timestamp, fields[start_index], cue.start_ms, 2) ||
                !parse_timestamp(ass_timestamp, fields[end_index], cue.end_ms, 2) ||
                cue.end_ms <= cue.start_ms)
                continue;

            cue.text = fields[text_index];
            cue.text.remove(QRegularExpression(QStringLiteral("\\{[^}]*\\}")));
            cue.text.replace(QStringLiteral("\\\\N"), QStringLiteral("\\n"));
            cue.text.replace(QStringLiteral("\\\\n"), QStringLiteral("\\n"));
            cue.text.replace(QStringLiteral("\\\\h"), QStringLiteral(" "));
            if (!cue.text.trimmed().isEmpty())
                cues.push_back(std::move(cue));
        }
    }
    else
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            if (!lines[i].contains(QStringLiteral("-->")))
                continue;

            const QStringList times = lines[i].split(QStringLiteral("-->"));
            if (times.size() != 2)
                continue;

            Cue cue;
            const QString end_timestamp = times[1].trimmed().section(QLatin1Char(' '), 0, 0);
            if (!parse_timestamp(srt_timestamp, times[0], cue.start_ms, 3) ||
                !parse_timestamp(srt_timestamp, end_timestamp, cue.end_ms, 3) ||
                cue.end_ms <= cue.start_ms)
                continue;

            QStringList text_lines;
            for (++i; i < lines.size() && !lines[i].trimmed().isEmpty(); ++i)
                text_lines.append(lines[i].trimmed());

            cue.text = text_lines.join(QLatin1Char('\\n'));
            cue.text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            cue.text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            cue.text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
            cue.text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
            if (!cue.text.isEmpty())
                cues.push_back(std::move(cue));
        }
    }

    std::sort(cues.begin(), cues.end(), [](const Cue& left, const Cue& right)
    {
        return left.start_ms < right.start_ms;
    });

    cues_ = std::move(cues);
    error.clear();
    return true;
}

QString SubtitleManager::text_at(int64_t position_ms, int64_t offset_ms, bool enabled) const
{
    if (!enabled)
        return {};

    position_ms -= offset_ms;
    const auto next = std::upper_bound(
        cues_.begin(),
        cues_.end(),
        position_ms,
        [](int64_t position, const Cue& cue)
        {
            return position < cue.start_ms;
        });
    if (next == cues_.begin())
        return {};

    const Cue& cue = *std::prev(next);
    return position_ms < cue.end_ms ? cue.text : QString();
}

int SubtitleManager::cue_count() const
{
    return static_cast<int>(cues_.size());
}

void SubtitleManager::clear()
{
    cues_.clear();
}
