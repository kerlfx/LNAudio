#include "subtitle_manager.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace
{

bool expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

bool write_file(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool test_srt_timeline_and_offset(const QString& directory)
{
    const QString path = directory + QStringLiteral("/sample.srt");
    const QByteArray data =
        "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello\r\nWorld\r\n\r\n";
    if (!expect(write_file(path, data), "write SRT fixture"))
        return false;

    SubtitleManager manager;
    QString error;
    if (!expect(manager.load_file(path, error), "load UTF-8 SRT"))
        return false;

    return expect(manager.cue_count() == 1, "SRT cue count") &&
        expect(manager.text_at(1500, 0, true) == QStringLiteral("Hello\nWorld"),
            "SRT text and line breaks") &&
        expect(manager.text_at(2500, 0, true).isEmpty(), "SRT end timestamp is exclusive") &&
        expect(manager.text_at(1700, 200, true) == QStringLiteral("Hello\nWorld"),
            "positive subtitle offset delays the cue") &&
        expect(manager.text_at(1500, 0, false).isEmpty(), "disabled subtitles return no text");
}

bool test_utf16le_bom(const QString& directory)
{
    const QString path = directory + QStringLiteral("/utf16.srt");
    const QString text = QStringLiteral(
        "1\r\n00:00:00,000 --> 00:00:01,000\r\n字幕测试\r\n");
    QByteArray data("\xFF\xFE", 2);
    data.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
    if (!expect(write_file(path, data), "write UTF-16LE SRT fixture"))
        return false;

    SubtitleManager manager;
    QString error;
    return expect(manager.load_file(path, error), "load UTF-16LE SRT") &&
        expect(manager.text_at(500, 0, true) == QStringLiteral("字幕测试"),
            "decode UTF-16LE subtitle text");
}

bool test_ass_fallback_parser(const QString& directory)
{
    const QString path = directory + QStringLiteral("/sample.ass");
    const QByteArray data =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\i1}Hi\\Nthere, with comma\n";
    if (!expect(write_file(path, data), "write ASS fixture"))
        return false;

    SubtitleManager manager;
    QString error;
    if (!expect(manager.load_file(path, error), "load ASS text fallback"))
        return false;

    return expect(manager.cue_count() == 1, "ASS dialogue cue count") &&
        expect(manager.text_at(1500, 0, true) ==
            QStringLiteral("Hi\nthere, with comma"),
            "ASS fallback strips override tags and preserves commas");
}

}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!expect(directory.isValid(), "create temporary subtitle directory"))
        return 1;
    if (!test_srt_timeline_and_offset(directory.path()))
        return 1;
    if (!test_utf16le_bom(directory.path()))
        return 1;
    if (!test_ass_fallback_parser(directory.path()))
        return 1;

    std::puts("LNPlayer subtitle tests passed.");
    return 0;
}
