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
    set(LNPLAYER_FFMPEG_REVISION "r0" CACHE STRING
        "Pinned FFmpeg MSVC prebuilt revision")
    set(LNPLAYER_FFMPEG_LICENSE "lgpl" CACHE STRING
        "FFmpeg prebuilt license variant (lgpl or gpl)")

    if(NOT LNPLAYER_FFMPEG_LICENSE STREQUAL "lgpl"
       AND NOT LNPLAYER_FFMPEG_LICENSE STREQUAL "gpl")
        message(FATAL_ERROR "LNPLAYER_FFMPEG_LICENSE must be 'lgpl' or 'gpl'.")
    endif()

    set(_ffmpeg_archive_name
        "ffmpeg-${LNPLAYER_FFMPEG_VERSION}-${LNPLAYER_FFMPEG_REVISION}_x64-windows-shared-${LNPLAYER_FFMPEG_LICENSE}.zip")
    set(_ffmpeg_archive
        "${CMAKE_BINARY_DIR}/_downloads/${_ffmpeg_archive_name}")
    set(_ffmpeg_url
        "https://github.com/System233/ffmpeg-msvc-prebuilt/releases/download/ffmpeg-${LNPLAYER_FFMPEG_VERSION}/${_ffmpeg_archive_name}")

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_downloads")
    message(STATUS "Downloading FFmpeg SDK: ${_ffmpeg_url}")
    file(DOWNLOAD
        "${_ffmpeg_url}"
        "${_ffmpeg_archive}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS _ffmpeg_download_status
    )

    list(GET _ffmpeg_download_status 0 _ffmpeg_download_code)
    list(GET _ffmpeg_download_status 1 _ffmpeg_download_message)
    if(NOT _ffmpeg_download_code EQUAL 0)
        file(REMOVE "${_ffmpeg_archive}")
        message(FATAL_ERROR
            "Failed to download FFmpeg SDK: ${_ffmpeg_download_message}")
    endif()

    file(MAKE_DIRECTORY "${LNPLAYER_FFMPEG_ROOT}")
    file(ARCHIVE_EXTRACT
        INPUT "${_ffmpeg_archive}"
        DESTINATION "${LNPLAYER_FFMPEG_ROOT}"
    )
endif()

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
)

target_include_directories(lnplayer
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE
        "${ASIO_INCLUDE_DIR}"
        "${FFMPEG_INCLUDE_DIR}"
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
