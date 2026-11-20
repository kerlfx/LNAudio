find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)

qt_add_executable(LNPlayerApp WIN32
    main.cpp
    main_window.cpp
    main_window.h
    d3d11_video_renderer.cpp
    d3d11_video_renderer.h
)

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
