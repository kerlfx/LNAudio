include(FetchContent)

# Keep FetchContent progress visible even when the build directory has an older cache.
message(STATUS "[LNPlayer] Checking third-party dependencies...")

set(LNPLAYER_ASIO_VERSION "1.38.2" CACHE STRING
    "Pinned standalone Asio version" FORCE)
string(REPLACE "." "-" LNPLAYER_ASIO_TAG_VERSION "${LNPLAYER_ASIO_VERSION}")
set(LNPLAYER_ASIO_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/asio/${LNPLAYER_ASIO_VERSION}" CACHE PATH
    "Version-specific root directory of the standalone Asio source" FORCE)
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

        FetchContent_Declare(
            Asio
            GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
            GIT_TAG "asio-${LNPLAYER_ASIO_TAG_VERSION}"
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            SOURCE_DIR "${LNPLAYER_ASIO_ROOT}"
            SOURCE_SUBDIR _lnplayer_headers_only
        )

        message(STATUS
            "[LNPlayer] Fetching standalone Asio ${LNPLAYER_ASIO_VERSION} into ${LNPLAYER_ASIO_ROOT}")
        FetchContent_MakeAvailable(Asio)
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

set(LNPLAYER_FFMPEG_VERSION "8.1.2" CACHE STRING
    "Pinned FFmpeg version used by the automatic SDK download" FORCE)
set(LNPLAYER_FFMPEG_LICENSE "lgpl" CACHE STRING
    "FFmpeg prebuilt license variant (lgpl or gpl)" FORCE)
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
        DOWNLOAD_NO_PROGRESS FALSE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${LNPLAYER_FFMPEG_ROOT}"
        SOURCE_SUBDIR _lnplayer_prebuilt_sdk
    )

    message(STATUS "[LNPlayer] Fetching prebuilt FFmpeg SDK into ${LNPLAYER_FFMPEG_ROOT}")
    FetchContent_MakeAvailable(FFmpeg)
endif()
message(STATUS "[LNPlayer] FFmpeg SDK ready.")

set(LNPLAYER_SQLITE_VERSION "3.54.1" CACHE STRING
    "Pinned SQLite amalgamation version" FORCE)
set(LNPLAYER_SQLITE_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/sqlite" CACHE PATH
    "Root directory of the SQLite amalgamation source")

if(NOT LNPLAYER_SQLITE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR
        "LNPLAYER_SQLITE_VERSION must use major.minor.patch format.")
endif()

set(_sqlite_major "${CMAKE_MATCH_1}")
set(_sqlite_minor "${CMAKE_MATCH_2}")
set(_sqlite_patch "${CMAKE_MATCH_3}")
if(_sqlite_minor LESS 10)
    set(_sqlite_minor "0${_sqlite_minor}")
endif()
if(_sqlite_patch LESS 10)
    set(_sqlite_patch "0${_sqlite_patch}")
endif()
set(_sqlite_version_number "${_sqlite_major}${_sqlite_minor}${_sqlite_patch}00")
set(_sqlite_archive_name "sqlite-amalgamation-${_sqlite_version_number}.zip")
set(_sqlite_source_dir "${LNPLAYER_SQLITE_ROOT}/${LNPLAYER_SQLITE_VERSION}")
set(_sqlite_amalgamation_dir "${_sqlite_source_dir}/sqlite-amalgamation-${_sqlite_version_number}")

message(STATUS "[LNPlayer] Checking SQLite ${LNPLAYER_SQLITE_VERSION}...")
if(NOT EXISTS "${_sqlite_amalgamation_dir}/sqlite3.c"
   AND NOT EXISTS "${_sqlite_source_dir}/sqlite3.c"
   AND NOT EXISTS "${LNPLAYER_SQLITE_ROOT}/sqlite3.c")
    # SQLite publishes a machine-readable CSV manifest inside download.html.
    # Resolve both the archive URL and SHA3-256 from that official manifest.
    set(_sqlite_metadata_dir "${FETCHCONTENT_BASE_DIR}/sqlite-metadata")
    file(MAKE_DIRECTORY "${_sqlite_metadata_dir}")
    set(_sqlite_metadata_file "${_sqlite_metadata_dir}/sqlite-download-products.html")
    message(STATUS "[LNPlayer] Fetching SQLite download metadata from sqlite.org...")
    file(DOWNLOAD
        "https://www.sqlite.org/download.html"
        "${_sqlite_metadata_file}"
        STATUS _sqlite_metadata_status
        SHOW_PROGRESS
        TLS_VERIFY ON
    )
    list(GET _sqlite_metadata_status 0 _sqlite_metadata_result)
    list(GET _sqlite_metadata_status 1 _sqlite_metadata_message)
    if(NOT _sqlite_metadata_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to retrieve SQLite download metadata: ${_sqlite_metadata_message}")
    endif()

    file(STRINGS "${_sqlite_metadata_file}" _sqlite_product_rows
        REGEX "^PRODUCT,${_sqlite_version_number},sqlite-amalgamation-${_sqlite_version_number}\\.zip,")
    list(LENGTH _sqlite_product_rows _sqlite_product_count)
    if(NOT _sqlite_product_count EQUAL 1)
        message(FATAL_ERROR
            "SQLite ${LNPLAYER_SQLITE_VERSION} amalgamation was not found exactly once "
            "in the official SQLite download manifest.")
    endif()

    list(GET _sqlite_product_rows 0 _sqlite_product_row)
    string(REPLACE "," ";" _sqlite_product_fields "${_sqlite_product_row}")
    list(LENGTH _sqlite_product_fields _sqlite_product_field_count)
    if(_sqlite_product_field_count LESS 5)
        message(FATAL_ERROR "Invalid SQLite download metadata row: ${_sqlite_product_row}")
    endif()
    list(GET _sqlite_product_fields 2 _sqlite_archive_relative_url)
    list(GET _sqlite_product_fields 4 _sqlite_archive_sha3)
    string(LENGTH "${_sqlite_archive_sha3}" _sqlite_archive_sha3_length)
    if(NOT _sqlite_archive_sha3_length EQUAL 64
       OR NOT _sqlite_archive_sha3 MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR
            "SQLite download manifest returned an invalid SHA3-256 for ${_sqlite_archive_name}.")
    endif()

    set(_sqlite_archive_url "https://www.sqlite.org/${_sqlite_archive_relative_url}")
    message(STATUS "[LNPlayer] SQLite archive: ${_sqlite_archive_url}")
    message(STATUS "[LNPlayer] SQLite SHA3-256 obtained from official manifest.")

    FetchContent_Declare(
        SQLite
        URL "${_sqlite_archive_url}"
        URL_HASH "SHA3_256=${_sqlite_archive_sha3}"
        DOWNLOAD_NO_PROGRESS FALSE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${_sqlite_source_dir}"
        SOURCE_SUBDIR _lnplayer_amalgamation_only
    )

    message(STATUS "[LNPlayer] Fetching SQLite ${LNPLAYER_SQLITE_VERSION} amalgamation")
    FetchContent_MakeAvailable(SQLite)
endif()
message(STATUS "[LNPlayer] SQLite ready.")



find_file(SQLITE3_C_SOURCE
    NAMES sqlite3.c
    PATHS
        "${_sqlite_amalgamation_dir}"
        "${_sqlite_source_dir}"
        "${LNPLAYER_SQLITE_ROOT}"
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
