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
| Standalone Asio | 1.38.2 | Header-only, standalone | Official GitHub repository; shallow clone of pinned tag |
| FFmpeg | 8.1.2 | Windows MSVC x64, shared, LGPL | System233 prebuilt release; resumable curl download; SHA-256 verified |
| SQLite | 3.54.1 | Generated amalgamation | Official SQLite source ZIP; resumable curl download; published SHA3-256 verified; generated with upstream build rules |

### Asio Git checkout

Standalone Asio 1.38.2 is obtained from the upstream repository
`https://github.com/chriskohlhoff/asio.git`, pinned to tag `asio-1-38-2`.
FetchContent uses a shallow clone and enables Git progress output. Git is
configured to use HTTP/1.1 for this checkout to avoid HTTP/2 transport
interoperability problems on some Windows network paths. Asio population uses
the project's default visible FetchContent output, including Git's transfer
progress. Git refreshes progress with carriage returns, so some IDE output
panes may display updates on one line rather than as separate lines.

The checkout is stored under `3rd_cache/asio/1.38.2-git`; the include directory
is its `include` subdirectory. An interrupted checkout is not considered ready:
CMake checks for `include/asio.hpp` before reusing the directory. If the checkout
is incomplete, remove that version-specific directory and configure again.



### FetchContent download progress

The project keeps `FETCHCONTENT_QUIET OFF` and enables Git's progress output
for the Asio clone. FetchContent already gives its ExternalProject population
download step direct terminal access: this is built into CMake's
`FetchContent/CMakeLists.cmake.in` (including CMake 3.24 and 4.3), where
`USES_TERMINAL_DOWNLOAD` is set to `YES`. FetchContent also reserves the
Ninja console job pool for that step.

Do not add `USES_TERMINAL_DOWNLOAD` to `FetchContent_Declare()` as a proposed
fix. FetchContent handles this internally; the option is consumed by its
argument parser rather than forwarded as a user-controlled setting. Adding it
does not improve progress rendering.

Git updates its progress line with carriage returns (`\\r`) rather than
emitting a newline for every percentage. A build/output pane that records
process output instead of behaving as an interactive terminal may display
those updates concatenated on one line. This is a presentation limitation,
not evidence that Git progress is disabled or that Ninja is dropping progress.
The exact rendering depends on the terminal/output host used to run CMake.

A Git transport error such as Schannel's `missing close_notify`, followed by
`early EOF` or an invalid pack index, is a separate failure: the clone did not
receive a complete pack. Terminal access and progress rendering do not repair
an interrupted TLS connection. Diagnose transport failures independently
from progress display.

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

CMake downloads this archive through `cmake/DownloadFile.cmake`, using the
system curl executable. Interrupted transfers retain a `.part` file and
subsequent configure attempts request continuation with HTTP byte ranges.
Curl's progress bar is shown during transfer. Transfers are retried up to
five times. The completed archive is SHA-256 verified before it is renamed into
the cache and extracted. A verified archive is reused on later configure runs.

### SQLite source and amalgamation generation

SQLite 3.54.1 is downloaded from the official SQLite release archive:

- URL: `https://sqlite.org/2026/sqlite-src-3540100.zip`
- SHA3-256: `877c6f4996ce7adb368686bce21638de9e3d91534832ad3d9d119037d1bc22ad`

The archive is the complete canonical source tree for SQLite 3.54.1. It is
downloaded through `cmake/DownloadFile.cmake`, which retains interrupted
transfers as `.part`, resumes using HTTP byte ranges, retries transient
failures up to five times, and verifies the official SHA3-256 before accepting
the archive. The verified ZIP is extracted under
`3rd_cache/sqlite/source-3.54.1`.

LNPlayer generates `sqlite3.c` and `sqlite3.h` from this source using SQLite's
official build rules:

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

The generated `sqlite3.c` is compiled directly into LNPlayer Core. The
generated `sqlite3.h` is staged into
`3rd_cache/sqlite/include-3.54.1`, which is the only SQLite include directory
added to the Core target. The SQLite source root is deliberately excluded from
the compiler include search path: its top-level file named `version` would
otherwise shadow MSVC's standard C++ `<version>` header. The amalgamation
source still finds its adjacent generated header through quoted-include lookup.

### Resumable dependency downloads

The shared `cmake/DownloadFile.cmake` helper uses curl's resume support and
keeps incomplete data in a sibling `.part` file. It retries up to five times
with a short increasing delay. If the server rejects a range request, the
helper discards the partial file and retries from byte zero. The finished
archive is accepted only after its pinned SHA-256 or SHA3-256 checksum passes,
then promoted to its final cache filename. Existing verified archives are
reused.

HTTP range support is controlled by the origin server and any intervening
proxy. When a server does not support ranges, the helper falls back to a
complete download; true continuation cannot be guaranteed for such a server.
Asio remains a shallow Git clone because Git clone does not provide a reliable
generic HTTP range-resume mechanism.

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

- `3rd_cache/asio/1.38.2-git`
- `3rd_cache/ffmpeg`
- `3rd_cache/sqlite/sqlite-src-3540100.zip` (verified source archive)
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
