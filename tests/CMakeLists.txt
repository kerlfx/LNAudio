add_executable(LNPlayerLifecycleTests
    lifecycle_tests.cpp
)

target_link_libraries(LNPlayerLifecycleTests
    PRIVATE
        lnplayer
)

add_test(NAME LNPlayer.Lifecycle COMMAND LNPlayerLifecycleTests)

add_custom_command(TARGET LNPlayerLifecycleTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:lnplayer>"
        "$<TARGET_FILE_DIR:LNPlayerLifecycleTests>"
    VERBATIM
)

file(GLOB LNPLAYER_TEST_FFMPEG_DLLS CONFIGURE_DEPENDS
    "${LNPLAYER_FFMPEG_ROOT}/bin/*.dll"
)

foreach(ffmpeg_dll IN LISTS LNPLAYER_TEST_FFMPEG_DLLS)
    add_custom_command(TARGET LNPlayerLifecycleTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ffmpeg_dll}"
            "$<TARGET_FILE_DIR:LNPlayerLifecycleTests>"
        VERBATIM
    )
endforeach()
