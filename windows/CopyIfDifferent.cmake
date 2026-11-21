if(NOT DEFINED LNPLAYER_DEPLOY_SOURCE OR NOT DEFINED LNPLAYER_DEPLOY_DESTINATION)
    message(FATAL_ERROR "LNPLAYER_DEPLOY_SOURCE and LNPLAYER_DEPLOY_DESTINATION are required")
endif()

file(GLOB_RECURSE _deploy_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${LNPLAYER_DEPLOY_SOURCE}"
    "${LNPLAYER_DEPLOY_SOURCE}/*"
)

foreach(_relative_file IN LISTS _deploy_files)
    set(_source_file "${LNPLAYER_DEPLOY_SOURCE}/${_relative_file}")
    set(_destination_file "${LNPLAYER_DEPLOY_DESTINATION}/${_relative_file}")
    get_filename_component(_destination_directory "${_destination_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_directory}")
    file(COPY_FILE
        "${_source_file}"
        "${_destination_file}"
        ONLY_IF_DIFFERENT
    )
endforeach()
