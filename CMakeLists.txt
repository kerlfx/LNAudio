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
    enable_language(RC)
    add_subdirectory(windows)

    option(LNPLAYER_BUILD_TESTS "Build LNPlayer native tests" OFF)
    if(LNPLAYER_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)

    set(CPACK_PACKAGE_NAME "LNPlayer")
    set(CPACK_PACKAGE_VENDOR "LNPlayer")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "LNPlayer")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LNPlayer Windows media player")
    set(CPACK_NSIS_DISPLAY_NAME "LNPlayer")
    set(CPACK_NSIS_PACKAGE_NAME "LNPlayer")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/kerlfx/LNPlayer")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA [=[
        CreateDirectory "$SMPROGRAMS\LNPlayer"
        CreateShortCut "$SMPROGRAMS\LNPlayer\LNPlayer Video.lnk" "$INSTDIR\LNPlayer.exe" "--video"
    ]=])
    set(CPACK_NSIS_DELETE_ICONS_EXTRA [=[
        Delete "$SMPROGRAMS\LNPlayer\LNPlayer Video.lnk"
        RMDir "$SMPROGRAMS\LNPlayer"
    ]=])
    # Install per-user under LocalAppData so the application-local Data directory
    # remains writable without requiring elevation.
    set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\Programs")

    set(LNPLAYER_VIDEO_EXTENSIONS
        mp4 mkv mov avi webm wmv flv mpeg mpg m4v ts mts m2ts
        3gp 3g2 vob ogv mxf asf f4v divx mpe mpv
    )

    set(_lnplayer_nsis_install [=[
        SetRegView 64
        WriteRegStr HKCU "Software\Classes\LNPlayer.Video" "" "LNPlayer Video"
        WriteRegStr HKCU "Software\Classes\LNPlayer.Video\DefaultIcon" "" '"$INSTDIR\LNPlayer.exe",0'
        WriteRegStr HKCU "Software\Classes\LNPlayer.Video\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" --video "%1"'
        WriteRegStr HKCU "Software\Classes\Applications\LNPlayer.exe\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" --video "%1"'
        WriteRegStr HKCU "Software\LNPlayer\Capabilities" "ApplicationName" "LNPlayer"
        WriteRegStr HKCU "Software\LNPlayer\Capabilities" "ApplicationDescription" "LNPlayer Windows video player"
        WriteRegStr HKCU "Software\RegisteredApplications" "LNPlayer" "Software\LNPlayer\Capabilities"
    ]=])

    set(_lnplayer_nsis_uninstall [=[
        SetRegView 64
        DeleteRegValue HKCU "Software\RegisteredApplications" "LNPlayer"
        DeleteRegKey HKCU "Software\LNPlayer\Capabilities"
        DeleteRegKey HKCU "Software\LNPlayer"
        DeleteRegKey HKCU "Software\Classes\LNPlayer.Video"
        DeleteRegKey HKCU "Software\Classes\Applications\LNPlayer.exe"
    ]=])

    foreach(extension IN LISTS LNPLAYER_VIDEO_EXTENSIONS)
        string(APPEND _lnplayer_nsis_install
            "        WriteRegStr HKCU \\"Software\\\\Classes\\\\Applications\\\\LNPlayer.exe\\\\SupportedTypes\\" \\".\${extension}\\" \\"\\"\\n"
            "        WriteRegStr HKCU \\"Software\\\\Classes\\\\.\${extension}\\\\OpenWithProgids\\" \\"LNPlayer.Video\\" \\"\\"\\n"
            "        WriteRegStr HKCU \\"Software\\\\LNPlayer\\\\Capabilities\\\\FileAssociations\\" \\".\${extension}\\" \\"LNPlayer.Video\\"\\n"
        )
        string(APPEND _lnplayer_nsis_uninstall
            "        DeleteRegValue HKCU \\"Software\\\\Classes\\\\.\${extension}\\\\OpenWithProgids\\" \\"LNPlayer.Video\\"\\n"
        )
    endforeach()

    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "\${_lnplayer_nsis_install}")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "\${_lnplayer_nsis_uninstall}")
    set(CPACK_GENERATOR "NSIS")
    include(CPack)
endif()
