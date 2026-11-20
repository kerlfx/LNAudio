#ifndef LNPLAYER_MUSIC_LIBRARY_H
#define LNPLAYER_MUSIC_LIBRARY_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace lnplayer
{

struct LibraryTrack
{
    int64_t id = 0;
    std::string path;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int64_t duration_ms = -1;
};

class MusicLibrary
{
public:
    MusicLibrary() = default;
    ~MusicLibrary();

    MusicLibrary(const MusicLibrary&) = delete;
    MusicLibrary& operator=(const MusicLibrary&) = delete;

    bool open_default(std::string& error);
    bool close();

    bool add_directory(const std::string& path, bool recursive, std::string& error);
    bool remove_directory(const std::string& path, std::string& error);
    bool scan_directory(const std::string& path, bool recursive, int& imported_count, std::string& error);

    bool upsert_track(
        const std::string& path,
        const std::string& content_hash,
        const std::string& title,
        const std::string& artist,
        const std::string& album,
        const std::string& genre,
        int64_t duration_ms,
        int64_t file_size,
        int64_t modified_time,
        int64_t& track_id,
        std::string& error);

    bool record_playback(
        int64_t track_id,
        int64_t position_ms,
        bool completed,
        std::string& error);

    std::vector<LibraryTrack> search_tracks(
        const std::string& query,
        int limit,
        std::string& error);

private:
    bool initialize_schema(std::string& error);
    bool execute(const char* sql, std::string& error) const;

    sqlite3* database_ = nullptr;
    mutable std::mutex mutex_;
};

}

#endif
