find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}" LNPLAYER_WINDOWS_SOURCES)

qt_add_executable(LNPlayerApp WIN32
    ${LNPLAYER_WINDOWS_SOURCES}
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
