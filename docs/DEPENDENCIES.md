# Third-party dependency baseline

This document records the dependency versions used by the Windows video player.
Version changes must update the CMake pins and this document together.

## Build toolchain

| Component | Version / configuration | Acquisition |
|---|---|---|
| C++ language | C++20 | MSVC |
| Visual Studio | VS 2022, x64 | Installed toolchain |
| CMake | 3.24 minimum; 4.3 used for current development | Installed tool |
| Qt | 6.12.0, MSVC 2022 x64 | Installed Qt SDK; not fetched by this project |
| Ninja | Optional generator | Installed build tool |

## Runtime / source dependencies

| Dependency | Pinned version | Variant | Acquisition / verification |
|---|---:|---|---|
| Standalone Asio | 1.38.2 | Header-only, standalone | Git tag `asio-1-38-2` via FetchContent |
| FFmpeg | 8.1.2 | Windows MSVC x64, shared, LGPL | System233 prebuilt release; SHA-256 checked by FetchContent |
| SQLite | 3.54.1 | Generated amalgamation | Official `sqlite/sqlite` GitHub tag; generated with upstream build rules |

### FFmpeg version policy

FFmpeg 8.1.3 is an upstream stable maintenance release, but the selected Windows
prebuilt provider currently advertises the 8.1.2 stable package and does not
provide a verifiable 8.1.3 MSVC x64 LGPL artifact. The project therefore remains
on 8.1.2 rather than constructing an unverified URL or checksum. Upgrade only
after the provider publishes the matching package and its integrity metadata,
or after the project adopts a reproducible in-house FFmpeg build.

The currently pinned archive is:

- URL:
  `https://github.com/System233/ffmpeg-msvc-prebuilt/releases/download/ffmpeg-8.1.2/ffmpeg-8.1.2_x64-windows-shared-lgpl.zip`
- SHA-256:
  `80dc78fd43aa32e7c3b75401d0aae8bcd260127e528999198ac9d0b033a854df`

### SQLite source and amalgamation generation

SQLite 3.54.1 is fetched from the official GitHub mirror
(`sqlite/sqlite`) at the `version-3.54.1` release tag. The mirror contains
the canonical source tree and SQLite's own code-generation scripts, but does
not commit the generated amalgamation files.

LNPlayer therefore generates `sqlite3.c` and `sqlite3.h` from that source
using the upstream build rules:

- On Windows build hosts, CMake invokes
  `nmake /f Makefile.msc sqlite3.c`. The makefile builds its bundled
  `jimsh0.exe` Jim Tcl interpreter and source-verification helper as needed,
  then runs the official source collection and amalgamation scripts.
- On non-Windows build hosts, CMake runs the documented `configure` step and
  invokes `make sqlite3.c`.

A full Tcl installation is not required to generate the SQLite core
amalgamation. The small Jim Tcl bootstrap interpreter is built from the
SQLite source tree. A full Tcl installation is needed for SQLite's test
suite and optional Tcl extension targets, which LNPlayer does not build.

The source checkout and generated files are stored under
`3rd_cache/sqlite/source-3.54.1`. The generated `sqlite3.c` is compiled
directly into LNPlayer Core; the generated `sqlite3.h` is used as its
include directory. This removes the dependency on parsing the SQLite website
download manifest and on downloading a pre-generated amalgamation archive.

### Repository-local proxy

CMake optionally includes `proxy.cmake` from the repository root before adding
the Core subdirectory, so proxy environment variables are set before any
FetchContent dependency download begins.

The repository includes a ready-to-edit `proxy.cmake` configuration file.
Its settings are commented out by default; uncomment the required
`set(ENV{HTTP_PROXY} ...)`, `set(ENV{HTTPS_PROXY} ...)`, or optional
`ALL_PROXY` / `NO_PROXY` entries when needed. Keep machine-specific proxy
addresses and credentials local; do not commit personal changes to this file.
If the file is absent, the configure process keeps its inherited environment
unchanged. The settings apply to the CMake configure process and its child
download processes, without changing system-wide proxy settings.

### FetchContent storage

By default, FetchContent source trees and download metadata live under:

`3rd_cache/fetchcontent`

The individual SDK/source roots are:

- `3rd_cache/asio/1.38.2`
- `3rd_cache/ffmpeg`
- `3rd_cache/sqlite/source-3.54.1`

An explicitly configured `FETCHCONTENT_BASE_DIR` is preserved. The old CMake
default `build/_deps` is replaced with the repository-local cache location.

## Update procedure

1. Verify the upstream stable version and the exact provider artifact.
2. Verify the archive naming convention, architecture, linkage and license.
3. Obtain integrity metadata from the authoritative provider; do not invent or
   reuse a checksum from a different archive.
4. Update CMake pins and this document in the same change.
5. Configure locally on the supported toolchain and inspect the actual download
   paths and extracted SDK layout.
6. Build and run the relevant tests manually. CI is not triggered automatically.
