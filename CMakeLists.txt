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

add_subdirectory(core)

if(WIN32)
    add_subdirectory(windows)
endif()
