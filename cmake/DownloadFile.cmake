# Download a file with resumable HTTP transfers and integrity verification.
# Required: URL, OUTPUT_FILE, CURL_EXECUTABLE.
# Optional: EXPECTED_SHA256 or EXPECTED_SHA3_256.

foreach(_required URL OUTPUT_FILE CURL_EXECUTABLE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "DownloadFile.cmake requires -D${_required}=...")
    endif()
endforeach()

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
set(_partial_file "${OUTPUT_FILE}.part")
set(_download_complete FALSE)
set(_resume TRUE)

foreach(_attempt RANGE 1 5)
    if(_resume)
        set(_resume_args --continue-at -)
    else()
        set(_resume_args)
    endif()

    message(STATUS "[LNPlayer] Download attempt ${_attempt}/5: ${URL}")
    execute_process(
        COMMAND "${CURL_EXECUTABLE}"
            --location --fail --silent --show-error
            --connect-timeout 30
            ${_resume_args}
            --output "${_partial_file}" "${URL}"
        RESULT_VARIABLE _curl_result
        ERROR_VARIABLE _curl_error
    )

    if(_curl_result EQUAL 0)
        set(_download_complete TRUE)
        break()
    endif()

    if(_resume AND "${_curl_result}" STREQUAL "33")
        message(STATUS "[LNPlayer] Range resume rejected; restarting from zero.")
        file(REMOVE "${_partial_file}")
        set(_resume FALSE)
    else()
        message(WARNING
            "[LNPlayer] Download attempt ${_attempt} failed (curl ${_curl_result}): ${_curl_error}")
    endif()

    if(_attempt LESS 5)
        math(EXPR _delay "2 * ${_attempt}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep "${_delay}")
    endif()
endforeach()

if(NOT _download_complete)
    message(FATAL_ERROR
        "Download failed after 5 attempts. Partial data is preserved at:\n"
        "  ${_partial_file}\nURL: ${URL}")
endif()

if(DEFINED EXPECTED_SHA256 AND NOT "${EXPECTED_SHA256}" STREQUAL "")
    file(SHA256 "${_partial_file}" _actual_hash)
    string(TOLOWER "${EXPECTED_SHA256}" _expected_hash)
    string(TOLOWER "${_actual_hash}" _actual_hash)
elseif(DEFINED EXPECTED_SHA3_256 AND NOT "${EXPECTED_SHA3_256}" STREQUAL "")
    file(SHA3_256 "${_partial_file}" _actual_hash)
    string(TOLOWER "${EXPECTED_SHA3_256}" _expected_hash)
    string(TOLOWER "${_actual_hash}" _actual_hash)
else()
    set(_expected_hash "")
endif()

if(NOT _expected_hash STREQUAL "" AND NOT _actual_hash STREQUAL _expected_hash)
    message(FATAL_ERROR
        "Checksum mismatch for ${URL}\nExpected: ${_expected_hash}\n"
        "Actual:   ${_actual_hash}\nPartial file preserved at ${_partial_file}")
endif()

file(REMOVE "${OUTPUT_FILE}")
file(RENAME "${_partial_file}" "${OUTPUT_FILE}")
message(STATUS "[LNPlayer] Download ready: ${OUTPUT_FILE}")
