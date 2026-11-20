add_executable(LNPlayerLifecycleTests
    lifecycle_tests.cpp
)

target_link_libraries(LNPlayerLifecycleTests
    PRIVATE
        lnplayer
)

add_test(NAME LNPlayer.Lifecycle COMMAND LNPlayerLifecycleTests)
