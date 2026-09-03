if(NOT DEFINED RUNTIME_SOURCE_DIR OR RUNTIME_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "RUNTIME_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE runtime_sources LIST_DIRECTORIES false
     "${RUNTIME_SOURCE_DIR}/*.c" "${RUNTIME_SOURCE_DIR}/*.cc"
     "${RUNTIME_SOURCE_DIR}/*.cpp" "${RUNTIME_SOURCE_DIR}/*.cxx"
     "${RUNTIME_SOURCE_DIR}/*.h" "${RUNTIME_SOURCE_DIR}/*.hpp"
     "${RUNTIME_SOURCE_DIR}/*.inl")
set(offenders "")
foreach(source_file IN LISTS runtime_sources)
    get_filename_component(source_name "${source_file}" NAME)
    if(source_name STREQUAL "status_emitter.cpp")
        continue()
    endif()
    file(READ "${source_file}" source_text)
    string(FIND "${source_text}" "AYTHER_STATUS" status_at)
    if(NOT status_at EQUAL -1)
        list(APPEND offenders "${source_file}")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " offender_list)
    message(FATAL_ERROR
        "AYTHER_STATUS may only be framed by status_emitter.cpp:\n  ${offender_list}")
endif()
