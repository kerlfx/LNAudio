message(STATUS "[LNPlayer] Checking third-party dependencies...")

# Download archives directly so CMake can show a single concise progress bar.
function(lnplayer_download_dependency)
    set(options STRIP_TOP_LEVEL)
    set(one_value_args NAME URL DESTINATION HASH)
    cmake_parse_arguments(DL "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT DL_NAME OR NOT DL_URL OR NOT DL_DESTINATION)
        message(FATAL_ERROR "lnplayer_download_dependency requires NAME, URL and DESTINATION.")
    endif()

    set(_download_dir "${PROJECT_SOURCE_DIR}/3rd_cache/downloads")
    set(_archive "${_download_dir}/${DL_NAME}.zip")
    set(_partial "${_archive}.part")
    set(_extract_dir "${_download_dir}/${DL_NAME}-extract")
    file(MAKE_DIRECTORY "${_download_dir}")

    set(_archive_valid FALSE)
    if(EXISTS "${_archive}")
        if(DL_HASH)
            string(REGEX MATCH "^([A-Za-z0-9_]+)=(.+)$" _hash_match "${DL_HASH}")
            if(NOT _hash_match)
                message(FATAL_ERROR "Invalid archive hash for ${DL_NAME}: ${DL_HASH}")
            endif()
            set(_hash_algorithm "${CMAKE_MATCH_1}")
            set(_expected_hash "${CMAKE_MATCH_2}")
            file(${_hash_algorithm} "${_archive}" _actual_hash)
            if(_actual_hash STREQUAL _expected_hash)
                set(_archive_valid TRUE)
            else()
                message(STATUS "[LNPlayer] Cached ${DL_NAME} archive failed hash check; downloading again.")
                file(REMOVE "${_archive}")
            endif()
        else()
            set(_archive_valid TRUE)
        endif()
    endif()

    if(NOT _archive_valid)
        message(STATUS "[LNPlayer] Downloading ${DL_NAME}...")
        file(REMOVE "${_partial}")
        if(DL_HASH)
            file(DOWNLOAD "${DL_URL}" "${_partial}"
                EXPECTED_HASH "${DL_HASH}"
                SHOW_PROGRESS
                TLS_VERIFY ON
                STATUS _download_status
                LOG _download_log
            )
        else()
            file(DOWNLOAD "${DL_URL}" "${_partial}"
                SHOW_PROGRESS
                TLS_VERIFY ON
                STATUS _download_status
                LOG _download_log
            )
        endif()

        list(GET _download_status 0 _download_code)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${_partial}")
            message(FATAL_ERROR
                "Failed to download ${DL_NAME} (${_download_status}).\n${_download_log}")
        endif()
        file(RENAME "${_partial}" "${_archive}")
    else()
        message(STATUS "[LNPlayer] Using cached ${DL_NAME} archive.")
    endif()

    message(STATUS "[LNPlayer] Extracting ${DL_NAME}...")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract_dir}")

    file(MAKE_DIRECTORY "${DL_DESTINATION}")
    if(DL_STRIP_TOP_LEVEL)
        file(GLOB _archive_entries LIST_DIRECTORIES TRUE "${_extract_dir}/*")
        list(LENGTH _archive_entries _entry_count)
        if(_entry_count EQUAL 1 AND IS_DIRECTORY "${_archive_entries}")
            list(GET _archive_entries 0 _archive_root)
            file(COPY "${_archive_root}/" DESTINATION "${DL_DESTINATION}")
        else()
            file(COPY "${_extract_dir}/" DESTINATION "${DL_DESTINATION}")
        endif()
    else()
        file(COPY "${_extract_dir}/" DESTINATION "${DL_DESTINATION}")
    endif()

    file(REMOVE_RECURSE "${_extract_dir}")
    message(STATUS "[LNPlayer] ${DL_NAME} ready.")
endfunction()

set(LNPLAYER_ASIO_VERSION "1.36.0" CACHE STRING
    "Pinned standalone Asio version")
string(REPLACE "." "-" LNPLAYER_ASIO_TAG_VERSION "${LNPLAYER_ASIO_VERSION}")
set(LNPLAYER_ASIO_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/asio" CACHE PATH
    "Root directory of the standalone Asio source")
option(LNPLAYER_DOWNLOAD_ASIO
    "Download the pinned standalone Asio headers when they are not available locally"
    ON
)

if(ASIO_INCLUDE_DIR AND NOT ASIO_INCLUDE_DIR MATCHES "-NOTFOUND$"
   AND NOT EXISTS "${ASIO_INCLUDE_DIR}/asio.hpp")
    message(FATAL_ERROR
        "ASIO_INCLUDE_DIR is set to '${ASIO_INCLUDE_DIR}', but asio.hpp was not found there.")
endif()

message(STATUS "[LNPlayer] Checking standalone Asio ${LNPLAYER_ASIO_VERSION}...")
if(NOT ASIO_INCLUDE_DIR OR ASIO_INCLUDE_DIR MATCHES "-NOTFOUND$")
    if(NOT EXISTS "${LNPLAYER_ASIO_ROOT}/asio/include/asio.hpp"
       AND NOT EXISTS "${LNPLAYER_ASIO_ROOT}/include/asio.hpp")
        if(NOT LNPLAYER_DOWNLOAD_ASIO)
            message(FATAL_ERROR
                "Standalone Asio was not found under '${LNPLAYER_ASIO_ROOT}'. "
                "Enable LNPLAYER_DOWNLOAD_ASIO or provide ASIO_INCLUDE_DIR.")
        endif()

        lnplayer_download_dependency(
            NAME "asio-${LNPLAYER_ASIO_VERSION}"
            URL "https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-${LNPLAYER_ASIO_TAG_VERSION}.zip"
            DESTINATION "${LNPLAYER_ASIO_ROOT}"
            STRIP_TOP_LEVEL
        )
    endif()
endif()

find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
        "${LNPLAYER_ASIO_ROOT}/asio/include"
        "${LNPLAYER_ASIO_ROOT}/include"
    REQUIRED
)
message(STATUS "[LNPlayer] Asio ready.")

set(LNPLAYER_FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/ffmpeg" CACHE PATH
    "Root directory of the FFmpeg development SDK")

option(LNPLAYER_DOWNLOAD_FFMPEG
    "Download the pinned MSVC FFmpeg SDK when it is not available locally"
    ON
)

message(STATUS "[LNPlayer] Checking FFmpeg SDK...")
if(NOT EXISTS "${LNPLAYER_FFMPEG_ROOT}/include/libavformat/avformat.h")
    if(NOT LNPLAYER_DOWNLOAD_FFMPEG)
        message(FATAL_ERROR
            "FFmpeg SDK not found at '${LNPLAYER_FFMPEG_ROOT}'. "
            "Enable LNPLAYER_DOWNLOAD_FFMPEG or provide LNPLAYER_FFMPEG_ROOT.")
    endif()

    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "Automatic FFmpeg download currently supports Windows MSVC x64 only. "
            "Provide LNPLAYER_FFMPEG_ROOT for other platforms/toolchains.")
    endif()

    set(LNPLAYER_FFMPEG_VERSION "8.1.2" CACHE STRING
        "Pinned FFmpeg version used by the automatic SDK download")
    set(LNPLAYER_FFMPEG_LICENSE "lgpl" CACHE STRING
        "FFmpeg prebuilt license variant (lgpl or gpl)")

    if(NOT LNPLAYER_FFMPEG_LICENSE STREQUAL "lgpl"
       AND NOT LNPLAYER_FFMPEG_LICENSE STREQUAL "gpl")
        message(FATAL_ERROR "LNPLAYER_FFMPEG_LICENSE must be 'lgpl' or 'gpl'.")
    endif()

    if(NOT LNPLAYER_FFMPEG_VERSION STREQUAL "8.1.2"
       OR NOT LNPLAYER_FFMPEG_LICENSE STREQUAL "lgpl")
        message(FATAL_ERROR
            "A verified SHA256 is currently provided only for FFmpeg 8.1.2 LGPL. "
            "Update the pinned archive checksum before selecting another variant.")
    endif()

    set(_ffmpeg_archive_name
        "ffmpeg-${LNPLAYER_FFMPEG_VERSION}_x64-windows-shared-${LNPLAYER_FFMPEG_LICENSE}.zip")
    set(_ffmpeg_url
        "https://github.com/System233/ffmpeg-msvc-prebuilt/releases/download/ffmpeg-${LNPLAYER_FFMPEG_VERSION}/${_ffmpeg_archive_name}")

    lnplayer_download_dependency(
        NAME "ffmpeg-${LNPLAYER_FFMPEG_VERSION}-${LNPLAYER_FFMPEG_LICENSE}"
        URL "${_ffmpeg_url}"
        HASH "SHA256=80dc78fd43aa32e7c3b75401d0aae8bcd260127e528999198ac9d0b033a854df"
        DESTINATION "${LNPLAYER_FFMPEG_ROOT}"
    )
endif()
message(STATUS "[LNPlayer] FFmpeg SDK ready.")

set(LNPLAYER_SQLITE_VERSION "3.50.4" CACHE STRING
    "Pinned SQLite amalgamation version")
set(LNPLAYER_SQLITE_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/sqlite" CACHE PATH
    "Root directory of the SQLite amalgamation source")

message(STATUS "[LNPlayer] Checking SQLite ${LNPLAYER_SQLITE_VERSION}...")
if(NOT EXISTS "${LNPLAYER_SQLITE_ROOT}/sqlite-amalgamation-3500400/sqlite3.c"
   AND NOT EXISTS "${LNPLAYER_SQLITE_ROOT}/sqlite3.c")
    lnplayer_download_dependency(
        NAME "sqlite-amalgamation-${LNPLAYER_SQLITE_VERSION}"
        URL "https://www2.sqlite.org/2025/sqlite-amalgamation-3500400.zip"
        HASH "SHA3_256=f131b68e6ba5fb891cc13ebb5ff955054c77294cb92d8d1268bad5dba4fa2a1"
        DESTINATION "${LNPLAYER_SQLITE_ROOT}"
    )
endif()
message(STATUS "[LNPlayer] SQLite ready.")

find_file(SQLITE3_C_SOURCE
    NAMES sqlite3.c
    PATHS
        "${LNPLAYER_SQLITE_ROOT}"
        "${LNPLAYER_SQLITE_ROOT}/sqlite-amalgamation-3500400"
    NO_DEFAULT_PATH
    REQUIRED
)
get_filename_component(SQLITE_INCLUDE_DIR "${SQLITE3_C_SOURCE}" DIRECTORY)
message(STATUS "[LNPlayer] Third-party dependencies ready.")

find_path(FFMPEG_INCLUDE_DIR
    NAMES libavformat/avformat.h
    HINTS "${LNPLAYER_FFMPEG_ROOT}/include"
    REQUIRED
)

find_library(FFMPEG_AVFORMAT_LIBRARY
    NAMES avformat avformat-62 avformat-61 avformat-60
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

find_library(FFMPEG_AVCODEC_LIBRARY
    NAMES avcodec avcodec-62 avcodec-61 avcodec-60
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

find_library(FFMPEG_AVFILTER_LIBRARY
    NAMES avfilter avfilter-11 avfilter-10 avfilter-9
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

find_library(FFMPEG_AVUTIL_LIBRARY
    NAMES avutil avutil-60 avutil-59 avutil-58
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

find_library(FFMPEG_SWRESAMPLE_LIBRARY
    NAMES swresample swresample-6 swresample-5 swresample-4
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

find_library(FFMPEG_SWSCALE_LIBRARY
    NAMES swscale swscale-9 swscale-8 swscale-7
    HINTS "${LNPLAYER_FFMPEG_ROOT}/lib"
    REQUIRED
)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}" LNPLAYER_CORE_SOURCES)

add_library(lnplayer SHARED
    ${LNPLAYER_CORE_SOURCES}
    "${SQLITE3_C_SOURCE}"
)

target_include_directories(lnplayer
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE
        "${ASIO_INCLUDE_DIR}"
        "${FFMPEG_INCLUDE_DIR}"
        "${SQLITE_INCLUDE_DIR}"
)

target_compile_definitions(lnplayer
    PRIVATE
        LNPLAYER_BUILD
        ASIO_STANDALONE
)

target_link_libraries(lnplayer
    PRIVATE
        "${FFMPEG_AVFORMAT_LIBRARY}"
        "${FFMPEG_AVCODEC_LIBRARY}"
        "${FFMPEG_AVFILTER_LIBRARY}"
        "${FFMPEG_AVUTIL_LIBRARY}"
        "${FFMPEG_SWRESAMPLE_LIBRARY}"
        "${FFMPEG_SWSCALE_LIBRARY}"
)

if(WIN32)
    target_link_libraries(lnplayer PRIVATE ole32 uuid)
endif()

set_target_properties(lnplayer PROPERTIES
    OUTPUT_NAME "lnplayer"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(MSVC)
    target_compile_options(lnplayer PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lnplayer PRIVATE -Wall -Wextra -Wpedantic)
endif()
