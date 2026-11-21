# LNPlayer

LNPlayer is a native Windows media player built with Qt 6 Widgets and a C++20 playback core. The current delivery target is the Windows video player. The audio-only UI and music-library work are not part of the current video completion milestone.

## Current architecture

- **Windows UI:** Qt 6 Widgets. Audio and video modes share the same executable; use --video for the video interface.
- **Core:** C++20 shared library exposed through a versioned C ABI.
- **Playback coordination:** Standalone Asio, one io_context thread and a shared two-worker thread pool. A per-Player strand serializes media commands.
- **Media:** FFmpeg for probing, demuxing, decoding, stream selection, subtitle rendering and pixel conversion.
- **Video output:** D3D11 renderer, D3D11VA hardware decode when available, and software decode fallback.
- **Persistence:** QSettings for the video queue. SQLite music-library initialization is lazy and is not required to start video playback.

The process owns at most one Player. Each independently launched application process owns its own Player instance.

## Windows build prerequisites

- Windows 10/11 x64.
- Visual Studio 2022 with the MSVC x64 C++ toolchain.
- CMake 3.24 or newer.
- Qt 6.12 or newer, Widgets component, built for the same MSVC toolchain.
- Network access during the first configure for the pinned standalone Asio, FFmpeg SDK and SQLite amalgamation downloads, unless those dependencies are already present in 3rd_cache.

Standalone Asio 1.38.2 is fetched from its pinned Git tag when no local headers are found; set `ASIO_INCLUDE_DIR` to use an existing installation or `LNPLAYER_DOWNLOAD_ASIO=OFF` to disable downloading. FFmpeg is pinned to the FFmpeg 8.1.2 LGPL MSVC x64 shared SDK because this is the latest 8.1.x package currently published by the selected prebuilt provider; CMake verifies its archive SHA-256 before extraction. The FFmpeg DLLs are copied beside the executable after build and included in the install tree. SQLite 3.54.1 is fetched as a single amalgamation source; its archive URL and SHA3-256 are read from SQLite's official download manifest at configure time. FetchContent sources and metadata are stored under `3rd_cache/fetchcontent` by default. No Boost dependency is used.

## Configure and build

Run from a Visual Studio Developer PowerShell or a shell with the MSVC environment configured:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
cmake --build build --config Release
~~~

To build the opt-in native lifecycle tests, configure with `-DLNPLAYER_BUILD_TESTS=ON`, then build `LNPlayerLifecycleTests` and run CTest manually. Tests are not run automatically by the build.

Adjust CMAKE_PREFIX_PATH to the installed Qt version and location. To use a pre-provisioned FFmpeg SDK:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" -DLNPLAYER_FFMPEG_ROOT="D:/SDK/ffmpeg" -DLNPLAYER_DOWNLOAD_FFMPEG=OFF
~~~

To use a pre-provisioned standalone Asio installation, pass `-DASIO_INCLUDE_DIR="D:/SDK/asio/include"`.

## Run

~~~powershell
.\build\windows\Release\LNPlayer.exe --video "D:\Media\sample.mkv"
~~~

Multiple media paths can be supplied in one invocation. Each application process owns one Player. Launching the executable again creates an independent window/process.

## Install and package

Install the runtime tree:

~~~powershell
cmake --install build --config Release --prefix staging
~~~

Qt's generated deployment script collects the required Qt runtime libraries and plugins. The install tree also contains LNPlayer, its Core DLL and FFmpeg runtime DLLs.

To create the Windows NSIS installer, install NSIS and run:

~~~powershell
cpack --config build/CPackConfig.cmake -C Release -G NSIS
~~~

The installer registers LNPlayer as an available application for common video extensions and adds it to Windows' application capabilities. It does not silently replace the user's default file associations; Windows controls the default-app selection UI.

## Video feature areas

The codebase includes FFmpeg video playback, D3D11VA/software decode paths, D3D11 presentation, screenshots, playback queue operations, frame stepping, audio/subtitle stream selection, external subtitles, subtitle overlays, rotation metadata, color metadata and video filters.

Feature presence is not the same as release acceptance. See docs/VIDEO_COMPLETION.md for remaining implementation, validation and packaging work. The checklist is authoritative for what has and has not been verified.

## Development constraints

- Work on gptdev; do not commit directly to main.
- Do not automatically run CI.
- Keep Core independent of Qt and Boost.
- Keep C++ headers as .h; keep a module's header and implementation together.
- Prefer aux_source_directory() for source collection.
