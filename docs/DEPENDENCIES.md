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
| SQLite | 3.54.1 | Amalgamation | Official SQLite download manifest supplies archive URL and SHA3-256 |

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

### SQLite metadata policy

The SQLite amalgamation version is converted to its numeric product identifier
(`3540100`). During configure, FetchContent downloads the official
`https://www.sqlite.org/download.html` manifest with `DOWNLOAD_NO_EXTRACT`,
so the HTML/CSV file remains intact for parsing. CMake selects the exact
`sqlite-amalgamation-3540100.zip` product row and reads its relative archive
URL and SHA3-256 field. FetchContent then downloads and verifies the archive.
No manually maintained SQLite checksum is embedded in CMake.

All network downloads (Asio, FFmpeg, the SQLite manifest, and the SQLite
amalgamation archive) now use FetchContent. The manifest is a raw-file download,
not an extracted source dependency; it is stored under
`3rd_cache/fetchcontent/sqlite-metadata/download`.

### FetchContent storage

By default, FetchContent source trees and download metadata live under:

`3rd_cache/fetchcontent`

The individual SDK/source roots are:

- `3rd_cache/asio/1.38.2`
- `3rd_cache/ffmpeg`
- `3rd_cache/sqlite/3.54.1`

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
