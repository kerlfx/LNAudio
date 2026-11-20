aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}" LNPLAYER_CORE_SOURCES)

add_library(lnplayer SHARED
    ${LNPLAYER_CORE_SOURCES}
)

target_include_directories(lnplayer
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_compile_definitions(lnplayer
    PRIVATE
        LNPLAYER_BUILD
)

set_target_properties(lnplayer PROPERTIES
    OUTPUT_NAME "lnplayer"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(MSVC)
    target_compile_options(lnplayer PRIVATE /W4 /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lnplayer PRIVATE -Wall -Wextra -Wpedantic)
endif()
