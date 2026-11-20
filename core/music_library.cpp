#include "music_library.h"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

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
    const wchar_t* local_app_data = _wgetenv(L"LOCALAPPDATA");
    if (!local_app_data)
    {
        error = "LOCALAPPDATA is not available.";
        return false;
    }
    const std::filesystem::path directory =
        std::filesystem::path(local_app_data) / L"LNPlayer";
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

    sqlite3_stmt* statement = nullptr;
    if (!prepare(database_,
        "INSERT INTO playback_history(track_id, position_ms, completed) VALUES(?, ?, ?);",
        &statement, error))
        return false;

    sqlite3_bind_int64(statement, 1, track_id);
    sqlite3_bind_int64(statement, 2, position_ms);
    sqlite3_bind_int(statement, 3, completed ? 1 : 0);
    bool success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!success)
    {
        error = sqlite3_errmsg(database_);
        return false;
    }

    if (!prepare(database_,
        "UPDATE tracks SET play_count=play_count+1, last_played_at=unixepoch(), updated_at=unixepoch() WHERE id=?;",
        &statement, error))
        return false;

    sqlite3_bind_int64(statement, 1, track_id);
    success = sqlite3_step(statement) == SQLITE_DONE;
    if (!success)
        error = sqlite3_errmsg(database_);
    sqlite3_finalize(statement);
    return success;
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
