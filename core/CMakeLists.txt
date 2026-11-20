find_path(ASIO_INCLUDE_DIR
    NAMES asio.hpp
    HINTS
        "${PROJECT_SOURCE_DIR}/3rd_cache/asio/asio/include"
        "${PROJECT_SOURCE_DIR}/3rd_cache/asio/include"
    REQUIRED
)

set(LNPLAYER_FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/3rd_cache/ffmpeg" CACHE PATH
    "Root directory of the FFmpeg development SDK")

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
)

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
