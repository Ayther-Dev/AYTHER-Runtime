cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED INPUT_FILE OR NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "INPUT_FILE must name a protocol-v1 JSONL fixture")
endif()
if(NOT DEFINED EXPECT_VALID)
    set(EXPECT_VALID ON)
endif()

set(valid TRUE)
set(diagnostic "")
file(READ "${INPUT_FILE}" raw ENCODING UTF-8)
if(raw STREQUAL "" OR NOT raw MATCHES "\n$")
    set(valid FALSE)
    set(diagnostic "stream is empty or ends with a partial line")
endif()

file(STRINGS "${INPUT_FILE}" records ENCODING UTF-8)
set(line_number 0)
set(allowed_reasons
    cli.invalid_argument
    core.load_failed
    core.invalid
    pack.rejected
    pack.no_active_subsystems
    state.restore_failed
    state.save_failed
    vulkan.unavailable
    vulkan.initialization_failed
    vulkan.frame_failed
    vulkan.postprocess_degraded
    persistence.config_invalid
    persistence.io_failed
    persistence.capture_failed
    protocol.incompatible)

macro(require_json_type member expected)
    if(valid)
        string(JSON actual_type ERROR_VARIABLE json_error
               TYPE "${json}" "${member}")
        if(NOT json_error STREQUAL "NOTFOUND" OR
           NOT actual_type STREQUAL "${expected}")
            set(valid FALSE)
            set(diagnostic
                "line ${line_number}: '${member}' must be ${expected}")
        endif()
    endif()
endmacro()

foreach(record IN LISTS records)
    math(EXPR line_number "${line_number} + 1")
    if(NOT valid)
        break()
    endif()
    if(NOT record MATCHES "^AYTHER_STATUS ")
        set(valid FALSE)
        set(diagnostic "line ${line_number}: missing AYTHER_STATUS prefix")
        break()
    endif()
    string(REGEX REPLACE "^AYTHER_STATUS " "" json "${record}")
    string(JSON root_type ERROR_VARIABLE parse_error TYPE "${json}")
    if(NOT parse_error STREQUAL "NOTFOUND" OR NOT root_type STREQUAL "OBJECT")
        set(valid FALSE)
        set(diagnostic "line ${line_number}: invalid JSON object")
        break()
    endif()

    require_json_type(protocol_version NUMBER)
    require_json_type(event STRING)
    if(NOT valid)
        break()
    endif()
    string(JSON version GET "${json}" protocol_version)
    string(JSON event GET "${json}" event)
    if(NOT version STREQUAL "1")
        set(valid FALSE)
        set(diagnostic "line ${line_number}: unsupported protocol_version=${version}")
        break()
    endif()

    if(event STREQUAL "probe")
        require_json_type(ok BOOLEAN)
        if(valid)
            string(JSON ok GET "${json}" ok)
            if(ok)
                require_json_type(api NUMBER)
                require_json_type(library_name STRING)
                require_json_type(library_version STRING)
                require_json_type(valid_extensions STRING)
                require_json_type(need_fullpath BOOLEAN)
                require_json_type(block_extract BOOLEAN)
            else()
                require_json_type(reason STRING)
            endif()
        endif()
    elseif(event STREQUAL "ready")
        require_json_type(game_id STRING)
        require_json_type(has_pack BOOLEAN)
        require_json_type(manifest STRING)
    elseif(event STREQUAL "now-playing")
        require_json_type(game_id STRING)
        require_json_type(title STRING)
    elseif(event STREQUAL "warning")
        require_json_type(reason STRING)
    elseif(event STREQUAL "crash-test" OR event STREQUAL "exit")
        # Envelope-only events. Optional fields remain forward compatible.
    else()
        # Stable v1 explicitly requires consumers to ignore unknown events.
    endif()

    if(valid AND (event STREQUAL "warning" OR event STREQUAL "probe"))
        string(JSON reason_type ERROR_VARIABLE reason_error TYPE "${json}" reason)
        if(reason_error STREQUAL "NOTFOUND")
            string(JSON reason GET "${json}" reason)
            if(NOT reason IN_LIST allowed_reasons)
                set(valid FALSE)
                set(diagnostic
                    "line ${line_number}: unknown stable reason '${reason}'")
            endif()
        endif()
    endif()
endforeach()

if(EXPECT_VALID AND NOT valid)
    message(FATAL_ERROR "expected valid protocol-v1 stream: ${diagnostic}")
elseif(NOT EXPECT_VALID AND valid)
    message(FATAL_ERROR "expected invalid protocol-v1 stream")
endif()

message(STATUS "protocol-v1 conformance: valid=${valid}; ${diagnostic}")
