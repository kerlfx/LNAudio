cmake_minimum_required(VERSION 3.24)

project(LNPlayer
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32 AND NOT ANDROID)
    message(FATAL_ERROR "LNPlayer currently supports Windows and Android builds only.")
endif()

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}/src/core" LNPLAYER_CORE_SOURCES)

add_library(lnplayer SHARED
    ${LNPLAYER_CORE_SOURCES}
)

target_include_directories(lnplayer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)

target_compile_definitions(lnplayer
    PRIVATE
        LNPLAYER_BUILD
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
