find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}" LNPLAYER_WINDOWS_SOURCES)

qt_add_executable(LNPlayerApp WIN32
    ${LNPLAYER_WINDOWS_SOURCES}
)

target_include_directories(LNPlayerApp PRIVATE "${ASIO_INCLUDE_DIR}")
target_compile_definitions(LNPlayerApp PRIVATE ASIO_STANDALONE)

target_link_libraries(LNPlayerApp
    PRIVATE
        Qt6::Widgets
        lnplayer
        d3d11
        dxgi
        d3dcompiler
)

set_target_properties(LNPlayerApp PROPERTIES
    OUTPUT_NAME "LNPlayer"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LNPlayer.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc"
    @ONLY
)
target_sources(LNPlayerApp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc")

file(GLOB LNPLAYER_FFMPEG_DLLS CONFIGURE_DEPENDS
    "${LNPLAYER_FFMPEG_ROOT}/bin/*.dll"
)

foreach(ffmpeg_dll IN LISTS LNPLAYER_FFMPEG_DLLS)
    add_custom_command(TARGET LNPlayerApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ffmpeg_dll}"
            "$<TARGET_FILE_DIR:LNPlayerApp>"
        VERBATIM
    )
endforeach()

install(TARGETS LNPlayerApp lnplayer
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)

if(LNPLAYER_FFMPEG_DLLS)
    install(FILES ${LNPLAYER_FFMPEG_DLLS} DESTINATION .)
endif()

qt_generate_deploy_app_script(
    TARGET LNPlayerApp
    OUTPUT_SCRIPT LNPLAYER_QT_DEPLOY_SCRIPT
)
install(SCRIPT "${LNPLAYER_QT_DEPLOY_SCRIPT}")
