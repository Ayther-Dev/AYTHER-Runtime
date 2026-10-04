cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED RUNTIME_EXE OR NOT EXISTS "${RUNTIME_EXE}")
    message(FATAL_ERROR "RUNTIME_EXE must name the built Runtime")
endif()

execute_process(
    COMMAND "${RUNTIME_EXE}" --qa-capabilities
            --core "must-not-be-opened.dll" --rom "must-not-be-opened.rom"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10)

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "capability query failed with ${result}\nstdout:\n${output}\nstderr:\n${errors}")
endif()

set(marker "AYTHER_QA_CAPABILITIES ")
string(FIND "${output}" "${marker}" marker_at)
if(marker_at EQUAL -1)
    message(FATAL_ERROR "capability query omitted its marker:\n${output}")
endif()
string(LENGTH "${marker}" marker_length)
math(EXPR json_at "${marker_at} + ${marker_length}")
string(SUBSTRING "${output}" ${json_at} -1 json_tail)
string(FIND "${json_tail}" "\n" line_end)
if(line_end EQUAL -1)
    set(json "${json_tail}")
else()
    string(SUBSTRING "${json_tail}" 0 ${line_end} json)
endif()
string(FIND "${json_tail}" "${marker}" duplicate_at)
if(NOT duplicate_at EQUAL -1)
    message(FATAL_ERROR "capability query emitted more than one report")
endif()

foreach(field IN ITEMS schema runtime_version engine_version contracts capabilities)
    string(JSON field_type ERROR_VARIABLE json_error TYPE "${json}" "${field}")
    if(NOT json_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR "invalid or missing JSON field '${field}': ${json_error}")
    endif()
endforeach()

string(JSON schema GET "${json}" schema)
string(JSON capability_count LENGTH "${json}" capabilities)
if(NOT schema STREQUAL "1.0")
    message(FATAL_ERROR "unexpected capability schema: ${json}")
endif()
if(EXPECT_FULL)
    foreach(contract IN ITEMS engine runtime evidence hd_state)
        string(JSON contract_version GET "${json}" contracts ${contract} 0)
        if(NOT contract_version STREQUAL "1.0")
            message(FATAL_ERROR "unexpected ${contract} contract: ${json}")
        endif()
    endforeach()
    # Spec 002, BR-135 (contracts.md C1): the Runtime offers protocol 1.0 and 1.1, and
    # inspection_v1 next to visible_replay_v1.
    string(JSON runtime_count LENGTH "${json}" contracts runtime)
    string(JSON runtime_v11 ERROR_VARIABLE runtime_error GET "${json}" contracts runtime 1)
    if(NOT runtime_count EQUAL 2 OR NOT runtime_v11 STREQUAL "1.1")
        message(FATAL_ERROR "Runtime does not offer protocol 1.1: ${json}")
    endif()
    if(NOT capability_count EQUAL 12)
        message(FATAL_ERROR "Runtime omitted required capabilities: ${json}")
    endif()
    string(JSON visible_capability GET "${json}" capabilities 10)
    string(JSON inspection_capability GET "${json}" capabilities 11)
    if(NOT visible_capability STREQUAL "visible_replay_v1" OR
       NOT inspection_capability STREQUAL "inspection_v1")
        message(FATAL_ERROR "Runtime omitted visible replay or inspection: ${json}")
    endif()
    foreach(limit IN ITEMS fact_bytes batch_bytes live_occurrences
                           audio_channels sample_rate cancel_milliseconds)
        string(JSON limit_value GET "${json}" limits ${limit})
        if(limit_value LESS_EQUAL 0)
            message(FATAL_ERROR "invalid ${limit} capability limit: ${json}")
        endif()
    endforeach()
else()
    string(JSON observation GET "${json}" contracts engine_observation)
    if(NOT observation STREQUAL "1.0" OR NOT capability_count EQUAL 0)
        message(FATAL_ERROR "ordinary Runtime overclaimed QA support: ${json}")
    endif()
endif()

foreach(forbidden IN ITEMS
        "AYTHER_RESERVATION"
        "SDL_Init"
        "must-not-be-opened.dll"
        "must-not-be-opened.rom")
    string(FIND "${output}${errors}" "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR
            "capability query touched or claimed forbidden state '${forbidden}'")
    endif()
endforeach()

message(STATUS "Runtime QA capability query: ${json}")
