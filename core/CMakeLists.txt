include(FetchContent)

# Keep FetchContent progress visible even when the build directory has an older cache.
message(STATUS "[LNPlayer] Checking third-party dependencies...")

set(LNPLAYER_ASIO_VERSION "1.38.2" CACHE STRING
    "Pinned standalone Asio version" FORCE)
set(LNPLAYER_ASIO_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/asio/${LNPLAYER_ASIO_VERSION}-git" CACHE PATH
    "Version-specific root directory of the standalone Asio Git checkout" FORCE)
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
    if(NOT EXISTS "${LNPLAYER_ASIO_ROOT}/include/asio.hpp")
        if(NOT LNPLAYER_DOWNLOAD_ASIO)
            message(FATAL_ERROR
                "Standalone Asio was not found under '${LNPLAYER_ASIO_ROOT}'. "
                "Enable LNPLAYER_DOWNLOAD_ASIO or provide ASIO_INCLUDE_DIR.")
        endif()

        FetchContent_Declare(
            AsioGit
            GIT_REPOSITORY "https://github.com/chriskohlhoff/asio.git"
            GIT_TAG "asio-1-38-2"
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            GIT_CONFIG http.version=HTTP/1.1
            SOURCE_DIR "${LNPLAYER_ASIO_ROOT}"
            SOURCE_SUBDIR _lnplayer_headers_only
        )

        message(STATUS
            "[LNPlayer] Cloning standalone Asio ${LNPLAYER_ASIO_VERSION} (tag asio-1-38-2) into ${LNPLAYER_ASIO_ROOT}")
        FetchContent_MakeAvailable(AsioGit)
    endif()
endif()

find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS "${LNPLAYER_ASIO_ROOT}/include"
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
    "Pinned SQLite source version" FORCE)
set(LNPLAYER_SQLITE_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/sqlite" CACHE PATH
    "Root directory of the SQLite source and generated amalgamation")

# Download the official tagged source archive rather than cloning SQLite's
# large Git repository history. The archive contains the canonical source tree
# and upstream amalgamation-generation tools.
set(_sqlite_source_dir "${LNPLAYER_SQLITE_ROOT}/source-${LNPLAYER_SQLITE_VERSION}")
FetchContent_Declare(
    sqlite_source
    URL "https://github.com/sqlite/sqlite/archive/refs/tags/version-${LNPLAYER_SQLITE_VERSION}.tar.gz"
    DOWNLOAD_NO_PROGRESS FALSE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${_sqlite_source_dir}"
    SOURCE_SUBDIR _lnplayer_sqlite_source_only
)

FetchContent_GetProperties(sqlite_source)
if(NOT sqlite_source_POPULATED)
    message(STATUS
        "[LNPlayer] Cloning official SQLite ${LNPLAYER_SQLITE_VERSION} source into ${_sqlite_source_dir}")
    FetchContent_Populate(sqlite_source)
endif()

set(SQLITE3_C_SOURCE "${_sqlite_source_dir}/sqlite3.c")
set(_sqlite_generated_header "${_sqlite_source_dir}/sqlite3.h")

if(CMAKE_HOST_WIN32)
    # SQLite's Makefile.msc builds its small Jim Tcl interpreter (jimsh0.exe)
    # and source-verification helper with the host MSVC tools, then runs the
    # official target_source/sqlite3.c generation rules.
    find_program(LNPLAYER_NMAKE_EXECUTABLE NAMES nmake REQUIRED)
    add_custom_command(
        OUTPUT
            "${SQLITE3_C_SOURCE}"
            "${_sqlite_generated_header}"
        COMMAND
            "${LNPLAYER_NMAKE_EXECUTABLE}" /nologo /f Makefile.msc sqlite3.c
        WORKING_DIRECTORY "${_sqlite_source_dir}"
        DEPENDS
            "${_sqlite_source_dir}/Makefile.msc"
            "${_sqlite_source_dir}/main.mk"
            "${_sqlite_source_dir}/tool/mksqlite3c.tcl"
            "${_sqlite_source_dir}/autosetup/jimsh0.c"
        COMMENT "Generating SQLite ${LNPLAYER_SQLITE_VERSION} amalgamation with the official MSVC makefile"
        VERBATIM
    )
else()
    # For non-Windows build hosts, use SQLite's documented configure + make
    # generation path. This builds host-side code generators only; sqlite3.c
    # is subsequently compiled by LNPlayer for the selected target platform.
    find_program(LNPLAYER_SH_EXECUTABLE NAMES sh REQUIRED)
    find_program(LNPLAYER_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

    execute_process(
        COMMAND "${LNPLAYER_SH_EXECUTABLE}" configure
        WORKING_DIRECTORY "${_sqlite_source_dir}"
        RESULT_VARIABLE _sqlite_configure_result
        OUTPUT_VARIABLE _sqlite_configure_output
        ERROR_VARIABLE _sqlite_configure_error
    )
    if(NOT _sqlite_configure_result EQUAL 0)
        message(FATAL_ERROR
            "SQLite ${LNPLAYER_SQLITE_VERSION} configure failed:\n"
            "${_sqlite_configure_output}\n${_sqlite_configure_error}")
    endif()

    add_custom_command(
        OUTPUT
            "${SQLITE3_C_SOURCE}"
            "${_sqlite_generated_header}"
        COMMAND "${LNPLAYER_MAKE_EXECUTABLE}" sqlite3.c
        WORKING_DIRECTORY "${_sqlite_source_dir}"
        DEPENDS
            "${_sqlite_source_dir}/Makefile"
            "${_sqlite_source_dir}/main.mk"
            "${_sqlite_source_dir}/tool/mksqlite3c.tcl"
        COMMENT "Generating SQLite ${LNPLAYER_SQLITE_VERSION} amalgamation with the official makefile"
        VERBATIM
    )
endif()

set_source_files_properties("${SQLITE3_C_SOURCE}" PROPERTIES GENERATED TRUE)
set(SQLITE_INCLUDE_DIR "${_sqlite_source_dir}")
message(STATUS "[LNPlayer] SQLite source ready; official amalgamation will be generated during the build.")
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
