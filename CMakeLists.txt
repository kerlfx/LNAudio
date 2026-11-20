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

    set(CPACK_PACKAGE_NAME "LNPlayer")
    set(CPACK_PACKAGE_VENDOR "LNPlayer")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "LNPlayer")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LNPlayer Windows media player")
    set(CPACK_NSIS_DISPLAY_NAME "LNPlayer")
    set(CPACK_NSIS_PACKAGE_NAME "LNPlayer")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/kerlfx/LNPlayer")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
        WriteRegStr HKCR "LNPlayer.Video" "" "LNPlayer Video"
        WriteRegStr HKCR "LNPlayer.Video\DefaultIcon" "" '"$INSTDIR\LNPlayer.exe",0'
        WriteRegStr HKCR "LNPlayer.Video\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" "%1"'
        WriteRegStr HKCR "Applications\LNPlayer.exe\shell\open\command" "" '"$INSTDIR\LNPlayer.exe" "%1"'
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mp4" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mkv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mov" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".avi" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".webm" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".wmv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".flv" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpeg" ""
        WriteRegStr HKCR "Applications\LNPlayer.exe\SupportedTypes" ".mpg" ""
        WriteRegStr HKCR ".mp4\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mkv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mov\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".avi\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".webm\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".wmv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".flv\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpeg\OpenWithProgids" "LNPlayer.Video" ""
        WriteRegStr HKCR ".mpg\OpenWithProgids" "LNPlayer.Video" ""
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
    ]=])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
        DeleteRegKey HKCR "LNPlayer.Video"
        DeleteRegKey HKCR "Applications\LNPlayer.exe"
        DeleteRegKey HKLM "Software\Clients\Media\LNPlayer"
    ]=])
    set(CPACK_GENERATOR "NSIS")
    include(CPack)
endif()
