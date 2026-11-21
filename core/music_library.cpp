#include "music_library.h"

#include <sqlite3.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cstdlib>
#endif

namespace lnplayer
{
namespace
{

std::string column_text(sqlite3_stmt* statement, int column)
{
    const auto* value = sqlite3_column_text(statement, column);
    return value ? reinterpret_cast<const char*>(value) : std::string();
}

bool prepare(sqlite3* database, const char* sql, sqlite3_stmt** statement, std::string& error)
{
    const int result = sqlite3_prepare_v2(database, sql, -1, statement, nullptr);
    if (result == SQLITE_OK)
        return true;

    error = sqlite3_errmsg(database);
    return false;
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value)
{
    return sqlite3_bind_text(
        statement,
        index,
        value.c_str(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT) == SQLITE_OK;
}

}

MusicLibrary::~MusicLibrary()
{
    close();
}

bool MusicLibrary::open_default(std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_)
        return true;

#ifdef _WIN32
    wchar_t* local_app_data = nullptr;
    size_t local_app_data_size = 0;
    if (_wdupenv_s(&local_app_data, &local_app_data_size, L"LOCALAPPDATA") != 0
        || !local_app_data)
    {
        error = "LOCALAPPDATA is not available.";
        return false;
    }
    const std::filesystem::path directory =
        std::filesystem::path(local_app_data) / L"LNPlayer";
    std::free(local_app_data);
    const std::filesystem::path database_path = directory / L"library.db";
#else
    const char* data_home = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path directory = data_home
        ? std::filesystem::path(data_home) / "lnplayer"
        : std::filesystem::path(home ? home : ".") / ".local/share/lnplayer";
    const std::filesystem::path database_path = directory / "library.db";
#endif

    std::filesystem::create_directories(directory);

#ifdef _WIN32
    int result = sqlite3_open16(database_path.c_str(), &database_);
#else
    const std::string database_file = database_path.string();
    int result = sqlite3_open(database_file.c_str(), &database_);
#endif
    if (result != SQLITE_OK)
    {
        error = database_ ? sqlite3_errmsg(database_) : "Unable to allocate SQLite connection.";
        if (database_)
            sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(database_, 5000);
    if (!execute("PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;", error)
        || !initialize_schema(error))
    {
        sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }

    return true;
}

bool MusicLibrary::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!database_)
        return true;

    const int result = sqlite3_close(database_);
    if (result != SQLITE_OK)
        return false;

    database_ = nullptr;
    return true;
}

bool MusicLibrary::initialize_schema(std::string& error)
{
    int version = 0;
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_, "PRAGMA user_version;", &statement, error))
        return false;

    if (sqlite3_step(statement) == SQLITE_ROW)
        version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);

    if (version > 1)
    {
        error = "The music library database was created by a newer LNPlayer version.";
        return false;
    }

    if (version == 1)
        return true;

    const char* schema =
        "BEGIN IMMEDIATE;"
        "CREATE TABLE tracks ("
        " id INTEGER PRIMARY KEY,"
        " content_hash TEXT UNIQUE,"
        " title TEXT NOT NULL DEFAULT '',"
        " artist TEXT NOT NULL DEFAULT '',"
        " album TEXT NOT NULL DEFAULT '',"
        " genre TEXT NOT NULL DEFAULT '',"
        " duration_ms INTEGER NOT NULL DEFAULT -1,"
        " play_count INTEGER NOT NULL DEFAULT 0,"
        " last_played_at INTEGER,"
        " created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        " updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE TABLE file_locations ("
        " id INTEGER PRIMARY KEY,"
        " track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
        " path TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        " file_size INTEGER NOT NULL DEFAULT 0,"
        " modified_time INTEGER NOT NULL DEFAULT 0,"
        " is_available INTEGER NOT NULL DEFAULT 1,"
        " imported_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        " last_seen_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE INDEX file_locations_track_id ON file_locations(track_id);"
        "CREATE TABLE library_directories ("
        " id INTEGER PRIMARY KEY,"
        " path TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        " recursive INTEGER NOT NULL DEFAULT 1,"
        " enabled INTEGER NOT NULL DEFAULT 1,"
        " last_scan_at INTEGER,"
        " last_scan_status INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE playback_history ("
        " id INTEGER PRIMARY KEY,"
        " track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
        " location_id INTEGER REFERENCES file_locations(id) ON DELETE SET NULL,"
        " played_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        " position_ms INTEGER NOT NULL DEFAULT 0,"
        " completed INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX playback_history_played_at ON playback_history(played_at DESC);"
        "CREATE INDEX playback_history_track_id ON playback_history(track_id);"
        "CREATE TABLE playlists ("
        " id INTEGER PRIMARY KEY,"
        " name TEXT NOT NULL,"
        " created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        " updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE TABLE playlist_items ("
        " id INTEGER PRIMARY KEY,"
        " playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        " track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
        " position INTEGER NOT NULL"
        ");"
        "CREATE INDEX playlist_items_order ON playlist_items(playlist_id, position);"
        "PRAGMA user_version = 1;"
        "COMMIT;";

    if (execute(schema, error))
        return true;

    sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
}

bool MusicLibrary::execute(const char* sql, std::string& error) const
{
    char* message = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK)
        return true;

    error = message ? message : sqlite3_errmsg(database_);
    sqlite3_free(message);
    return false;
}

bool MusicLibrary::add_directory(
    const std::string& path,
    bool recursive,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_,
        "INSERT INTO library_directories(path, recursive, enabled) VALUES(?, ?, 1) "
        "ON CONFLICT(path) DO UPDATE SET recursive=excluded.recursive, enabled=1;",
        &statement, error))
        return false;

    bind_text(statement, 1, path);
    sqlite3_bind_int(statement, 2, recursive ? 1 : 0);
    const bool success = sqlite3_step(statement) == SQLITE_DONE;
    if (!success)
        error = sqlite3_errmsg(database_);
    sqlite3_finalize(statement);
    return success;
}

bool MusicLibrary::remove_directory(const std::string& path, std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_, "DELETE FROM library_directories WHERE path = ?;", &statement, error))
        return false;

    bind_text(statement, 1, path);
    const bool success = sqlite3_step(statement) == SQLITE_DONE;
    if (!success)
        error = sqlite3_errmsg(database_);
    sqlite3_finalize(statement);
    return success;
}


bool MusicLibrary::scan_directory(
    const std::string& path,
    bool recursive,
    int& imported_count,
    std::string& error)
{
    imported_count = 0;
    if (!add_directory(path, recursive, error))
        return false;

    const std::u8string utf8_path(
        reinterpret_cast<const char8_t*>(path.data()), path.size());
    const std::filesystem::path root(utf8_path);
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    auto import_file = [this, &imported_count, &error](const std::filesystem::directory_entry& entry)
    {
        if (!entry.is_regular_file())
            return;

        const std::string extension = entry.path().extension().string();
        static const std::vector<std::string> supported = {
            ".mp3", ".flac", ".wav", ".aac", ".m4a", ".ogg", ".opus",
            ".wma", ".aiff", ".aif", ".ape", ".mp4", ".mkv", ".mov",
            ".avi", ".webm"
        };
        bool accepted = false;
        for (const auto& item : supported)
        {
            if (extension.size() == item.size()
                && std::equal(extension.begin(), extension.end(), item.begin(),
                    [](unsigned char left, unsigned char right)
                    {
                        return std::tolower(left) == right;
                    }))
            {
                accepted = true;
                break;
            }
        }
        if (!accepted)
            return;

        const std::filesystem::path file_path = entry.path();
        const std::string utf8_path = file_path.generic_u8string().empty()
            ? std::string()
            : [&file_path]
              {
                  const auto value = file_path.generic_u8string();
                  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
              }();

        const auto size = entry.file_size();
        const auto modified = entry.last_write_time();
        const auto system_modified = std::chrono::time_point_cast<std::chrono::seconds>(
            modified - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now());
        const int64_t modified_time = system_modified.time_since_epoch().count();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            sqlite3_stmt* unchanged = nullptr;
            if (sqlite3_prepare_v2(
                    database_,
                    "SELECT file_size, modified_time FROM file_locations WHERE path=?;",
                    -1,
                    &unchanged,
                    nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(
                    unchanged, 1, utf8_path.c_str(),
                    static_cast<int>(utf8_path.size()), SQLITE_TRANSIENT);
                const bool same = sqlite3_step(unchanged) == SQLITE_ROW
                    && sqlite3_column_int64(unchanged, 0) == static_cast<int64_t>(size)
                    && sqlite3_column_int64(unchanged, 1) == modified_time;
                sqlite3_finalize(unchanged);
                if (same)
                {
                    sqlite3_stmt* touch = nullptr;
                    if (sqlite3_prepare_v2(
                            database_,
                            "UPDATE file_locations SET is_available=1, last_seen_at=unixepoch() WHERE path=?;",
                            -1,
                            &touch,
                            nullptr) == SQLITE_OK)
                    {
                        sqlite3_bind_text(
                            touch, 1, utf8_path.c_str(),
                            static_cast<int>(utf8_path.size()), SQLITE_TRANSIENT);
                        sqlite3_step(touch);
                        sqlite3_finalize(touch);
                        return;
                    }
                }
            }
            else if (unchanged)
            {
                sqlite3_finalize(unchanged);
            }
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file)
            return;

        AVSHA* sha = av_sha_alloc();
        if (!sha)
            return;
        if (av_sha_init(sha, 256) < 0)
        {
            av_free(sha);
            return;
        }

        std::array<uint8_t, 64 * 1024> buffer{};
        while (file)
        {
            file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
            const std::streamsize count = file.gcount();
            if (count > 0)
                av_sha_update(sha, buffer.data(), static_cast<unsigned int>(count));
        }
        if (!file.eof())
        {
            av_free(sha);
            return;
        }

        std::array<uint8_t, 32> digest{};
        av_sha_final(sha, digest.data());
        av_free(sha);

        std::ostringstream hash;
        hash << std::hex << std::setfill('0');
        for (uint8_t byte : digest)
            hash << std::setw(2) << static_cast<unsigned int>(byte);

        AVFormatContext* format = nullptr;
        if (avformat_open_input(&format, utf8_path.c_str(), nullptr, nullptr) < 0)
            return;
        if (avformat_find_stream_info(format, nullptr) < 0)
        {
            avformat_close_input(&format);
            return;
        }

        if (av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) < 0)
        {
            avformat_close_input(&format);
            return;
        }

        auto metadata = [format](const char* key)
        {
            const AVDictionaryEntry* value = av_dict_get(format->metadata, key, nullptr, 0);
            return value && value->value ? std::string(value->value) : std::string();
        };

        const std::string media_title = metadata("title");
        const auto filename_title = file_path.stem().u8string();
        const std::string title = media_title.empty()
            ? std::string(reinterpret_cast<const char*>(filename_title.data()), filename_title.size())
            : media_title;
        const std::string artist = metadata("artist");
        const std::string album = metadata("album");
        const std::string genre = metadata("genre");
        const int64_t duration_ms = format->duration == AV_NOPTS_VALUE
            ? -1
            : av_rescale_q(format->duration, AVRational{1, AV_TIME_BASE}, AVRational{1, 1000});
        avformat_close_input(&format);

        int64_t track_id = 0;
        std::string import_error;
        if (upsert_track(
            utf8_path, hash.str(), title, artist, album, genre,
            duration_ms, static_cast<int64_t>(size), modified_time,
            track_id, import_error))
            ++imported_count;
        else
            error = std::move(import_error);
    };

    if (recursive)
    {
        for (std::filesystem::recursive_directory_iterator iterator(root, options), end;
             iterator != end; ++iterator)
            import_file(*iterator);
    }
    else
    {
        for (std::filesystem::directory_iterator iterator(root, options), end;
             iterator != end; ++iterator)
            import_file(*iterator);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_,
        "UPDATE library_directories SET last_scan_at=unixepoch(), last_scan_status=1 WHERE path=?;",
        &statement, error))
        return false;
    bind_text(statement, 1, path);
    const bool success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return success;
}

bool MusicLibrary::upsert_track(
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
    std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!database_)
    {
        error = "Music library is not open.";
        return false;
    }

    if (!execute("BEGIN IMMEDIATE;", error))
        return false;

    sqlite3_stmt* statement = nullptr;
    const char* lookup_sql = content_hash.empty()
        ? "SELECT t.id FROM tracks t JOIN file_locations f ON f.track_id=t.id WHERE f.path=?;"
        : "SELECT id FROM tracks WHERE content_hash=?;";

    if (!prepare(database_, lookup_sql, &statement, error))
        goto rollback;

    bind_text(statement, 1, content_hash.empty() ? path : content_hash);
    if (sqlite3_step(statement) == SQLITE_ROW)
        track_id = sqlite3_column_int64(statement, 0);
    else
        track_id = 0;
    sqlite3_finalize(statement);
    statement = nullptr;

    if (track_id != 0)
    {
        if (!prepare(database_,
            "UPDATE tracks SET title=?, artist=?, album=?, genre=?, duration_ms=?, updated_at=unixepoch() WHERE id=?;",
            &statement, error))
            goto rollback;
        bind_text(statement, 1, title);
        bind_text(statement, 2, artist);
        bind_text(statement, 3, album);
        bind_text(statement, 4, genre);
        sqlite3_bind_int64(statement, 5, duration_ms);
        sqlite3_bind_int64(statement, 6, track_id);
    }
    else
    {
        if (!prepare(database_,
            "INSERT INTO tracks(content_hash, title, artist, album, genre, duration_ms) VALUES(?, ?, ?, ?, ?, ?);",
            &statement, error))
            goto rollback;
        if (content_hash.empty())
            sqlite3_bind_null(statement, 1);
        else
            bind_text(statement, 1, content_hash);
        bind_text(statement, 2, title);
        bind_text(statement, 3, artist);
        bind_text(statement, 4, album);
        bind_text(statement, 5, genre);
        sqlite3_bind_int64(statement, 6, duration_ms);
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        error = sqlite3_errmsg(database_);
        sqlite3_finalize(statement);
        statement = nullptr;
        goto rollback;
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (track_id == 0)
        track_id = sqlite3_last_insert_rowid(database_);

    if (!prepare(database_,
        "INSERT INTO file_locations(track_id, path, file_size, modified_time) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET track_id=excluded.track_id, file_size=excluded.file_size, "
        "modified_time=excluded.modified_time, is_available=1, last_seen_at=unixepoch();",
        &statement, error))
        goto rollback;

    sqlite3_bind_int64(statement, 1, track_id);
    bind_text(statement, 2, path);
    sqlite3_bind_int64(statement, 3, file_size);
    sqlite3_bind_int64(statement, 4, modified_time);
    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        error = sqlite3_errmsg(database_);
        sqlite3_finalize(statement);
        statement = nullptr;
        goto rollback;
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (!execute(
        "DELETE FROM tracks WHERE NOT EXISTS "
        "(SELECT 1 FROM file_locations WHERE file_locations.track_id=tracks.id);",
        error))
        goto rollback;

    if (execute("COMMIT;", error))
        return true;

rollback:
    if (statement)
        sqlite3_finalize(statement);
    sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
}

bool MusicLibrary::record_playback(
    int64_t track_id,
    int64_t position_ms,
    bool completed,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!database_)
    {
        error = "Music library is not open.";
        return false;
    }

    if (!execute("BEGIN IMMEDIATE;", error))
        return false;

    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_,
        "INSERT INTO playback_history(track_id, position_ms, completed) VALUES(?, ?, ?);",
        &statement, error))
        goto rollback;

    sqlite3_bind_int64(statement, 1, track_id);
    sqlite3_bind_int64(statement, 2, position_ms);
    sqlite3_bind_int(statement, 3, completed ? 1 : 0);
    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        error = sqlite3_errmsg(database_);
        sqlite3_finalize(statement);
        statement = nullptr;
        goto rollback;
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (!prepare(database_,
        "UPDATE tracks SET play_count=play_count+1, last_played_at=unixepoch(), updated_at=unixepoch() WHERE id=?;",
        &statement, error))
        goto rollback;

    sqlite3_bind_int64(statement, 1, track_id);
    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        error = sqlite3_errmsg(database_);
        sqlite3_finalize(statement);
        statement = nullptr;
        goto rollback;
    }
    sqlite3_finalize(statement);

    return execute("COMMIT;", error);

rollback:
    if (statement)
        sqlite3_finalize(statement);
    sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
}

std::vector<LibraryTrack> MusicLibrary::search_tracks(
    const std::string& query,
    int limit,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LibraryTrack> tracks;
    if (!database_)
    {
        error = "Music library is not open.";
        return tracks;
    }

    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_,
        "SELECT t.id, MIN(f.path), t.title, t.artist, t.album, t.genre, t.duration_ms "
        "FROM tracks t JOIN file_locations f ON f.track_id=t.id "
        "WHERE ? = '' OR instr(lower(t.title || ' ' || t.artist || ' ' || t.album), lower(?)) > 0 "
        "GROUP BY t.id ORDER BY t.artist COLLATE NOCASE, t.album COLLATE NOCASE, t.title COLLATE NOCASE "
        "LIMIT ?;",
        &statement, error))
        return tracks;

    bind_text(statement, 1, query);
    bind_text(statement, 2, query);
    sqlite3_bind_int(statement, 3, limit > 0 ? limit : 100);

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        LibraryTrack track;
        track.id = sqlite3_column_int64(statement, 0);
        track.path = column_text(statement, 1);
        track.title = column_text(statement, 2);
        track.artist = column_text(statement, 3);
        track.album = column_text(statement, 4);
        track.genre = column_text(statement, 5);
        track.duration_ms = sqlite3_column_int64(statement, 6);
        tracks.push_back(std::move(track));
    }

    sqlite3_finalize(statement);
    return tracks;
}

}
