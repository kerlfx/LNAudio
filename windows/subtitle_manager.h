#ifndef LNPLAYER_SUBTITLE_MANAGER_H
#define LNPLAYER_SUBTITLE_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

#include <QString>

class SubtitleManager
{
public:
    bool load_file(const QString& path, QString& error);
    QString text_at(int64_t position_ms, int64_t offset_ms, bool enabled) const;
    int cue_count() const;
    void clear();

private:
    struct Cue
    {
        int64_t start_ms = 0;
        int64_t end_ms = 0;
        QString text;
    };

    std::vector<Cue> cues_;
};

#endif
