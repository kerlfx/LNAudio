find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)

qt_add_executable(LNPlayerApp WIN32
    main.cpp
    main_window.cpp
    main_window.h
)

target_link_libraries(LNPlayerApp
    PRIVATE
        Qt6::Widgets
        lnplayer
)

set_target_properties(LNPlayerApp PROPERTIES
    OUTPUT_NAME "LNPlayer"
)
