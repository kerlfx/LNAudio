include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/3rd_cache/fetchcontent" CACHE PATH
    "Base directory for FetchContent downloads and build metadata")

find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
        "${PROJECT_SOURCE_DIR}/3rd_cache/asio/asio/include"
        "${PROJECT_SOURCE_DIR}/3rd_cache/asio/include"
    REQUIRED
)

set(LNPLAYER_FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/ffmpeg" CACHE PATH
    "Root directory of the FFmpeg development SDK")

option(LNPLAYER_DOWNLOAD_FFMPEG
    "Download the pinned MSVC FFmpeg SDK when it is not available locally"
    ON
)

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

    FetchContent_Declare(
        FFmpeg
        URL "${_ffmpeg_url}"
        URL_HASH SHA256=80dc78fd43aa32e7c3b75401d0aae8bcd260127e528999198ac9d0b033a854df
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${LNPLAYER_FFMPEG_ROOT}"
    )

    FetchContent_GetProperties(FFmpeg)
    if(NOT ffmpeg_POPULATED)
        message(STATUS "Fetching prebuilt FFmpeg SDK into ${LNPLAYER_FFMPEG_ROOT}")
        FetchContent_Populate(FFmpeg)
    endif()
endif()

set(LNPLAYER_SQLITE_VERSION "3.50.4" CACHE STRING
    "Pinned SQLite amalgamation version")
set(LNPLAYER_SQLITE_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/sqlite" CACHE PATH
    "Root directory of the SQLite amalgamation source")

if(NOT EXISTS "${LNPLAYER_SQLITE_ROOT}/sqlite-amalgamation-3500400/sqlite3.c"
   AND NOT EXISTS "${LNPLAYER_SQLITE_ROOT}/sqlite3.c")
    FetchContent_Declare(
        SQLite
        URL "https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip"
        URL_HASH SHA3_256=f131b68e6ba5fb891cc13ebb5ff955054c77294cb92d8d1268bad5dba4fa2a1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${LNPLAYER_SQLITE_ROOT}"
    )

    FetchContent_GetProperties(SQLite)
    if(NOT sqlite_POPULATED)
        message(STATUS "Fetching SQLite ${LNPLAYER_SQLITE_VERSION} amalgamation")
        FetchContent_Populate(SQLite)
    endif()
endif()

find_file(SQLITE3_C_SOURCE
    NAMES sqlite3.c
    PATHS
        "${LNPLAYER_SQLITE_ROOT}"
        "${LNPLAYER_SQLITE_ROOT}/sqlite-amalgamation-3500400"
    NO_DEFAULT_PATH
    REQUIRED
)
get_filename_component(SQLITE_INCLUDE_DIR "${SQLITE3_C_SOURCE}" DIRECTORY)

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
        "${FFMPEG_AVUTIL_LIBRARY}"
        "${FFMPEG_SWRESAMPLE_LIBRARY}"
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
