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
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
        SetRegView 64
        WriteRegStr HKCR "LNPlayer.Video" "" "LNPlayer Video"
        WriteRegStr HKCR "LNPlayer.Video\DefaultIcon" "" '"$INSTDIR\LNPlayer.exe",0'
        WriteRegStr HKCR "LNPlayer.Video\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" --video "%1"'
        WriteRegStr HKCR "Applications\LNPlayer.exe\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" --video "%1"'
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mp4" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mkv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mov" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".avi" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".webm" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".wmv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".flv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpeg" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpg" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".m4v" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".ts" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mts" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".m2ts" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".3gp" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".3g2" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".vob" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".ogv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mxf" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".asf" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".f4v" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".divx" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpe" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpv" ""
        WriteRegStr HKCR ".mp4\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mkv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mov\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".avi\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".webm\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".wmv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".flv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpeg\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpg\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".m4v\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".ts\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mts\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".m2ts\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".3gp\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".3g2\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".vob\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".ogv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mxf\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".asf\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".f4v\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".divx\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpe\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKLM "Software\RegisteredApplications" "LNPlayer" "Software\Clients\Media\LNPlayer\Capabilities"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities" "ApplicationName" "LNPlayer"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities" "ApplicationDescription" "LNPlayer Windows media player"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mp4" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mkv" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mov" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".avi" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".webm" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".wmv" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".flv" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mpeg" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mpg" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".m4v" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".ts" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mts" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".m2ts" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".3gp" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".3g2" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".vob" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".ogv" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mxf" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".asf" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".f4v" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".divx" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mpe" "LNPlayer.Video"
        WriteRegStr HKLM "Software\Clients\Media\LNPlayer\Capabilities\FileAssociations" ".mpv" "LNPlayer.Video"
    ]=])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
        SetRegView 64
        DeleteRegValue HKCR ".mp4\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mkv\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mov\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".avi\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".webm\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".wmv\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".flv\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mpeg\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mpg\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".m4v\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".ts\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mts\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".m2ts\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".3gp\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".3g2\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".vob\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".ogv\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mxf\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".asf\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".f4v\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".divx\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mpe\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKCR ".mpv\OpenWithProgids" "LNPlayer.Video"
        DeleteRegValue HKLM "Software\RegisteredApplications" "LNPlayer"
        DeleteRegKey HKCR "LNPlayer.Video"
        DeleteRegKey HKCR "Applications\LNPlayer.exe"
        DeleteRegKey HKLM "Software\Clients\Media\LNPlayer"
    ]=])
    set(CPACK_GENERATOR "NSIS")
    include(CPack)
endif()
