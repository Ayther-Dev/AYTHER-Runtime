cmake_minimum_required(VERSION 3.25)

# Spec 002, BR-157 (RNF-6, RNF-8): the CPack component `qa` contains the launcher
# `ayther_replay_qa` and the supervisor `ayther_audio_qa`; the `Runtime` component does
# not contain either, so a player installation never carries the QA tools.
foreach(required IN ITEMS BUILD_DIR CONFIG WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK_DIR}")

function(install_component component prefix)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
                --component ${component} --prefix "${prefix}"
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "installing ${component} failed (${code}):\n${out}${err}")
    endif()
endfunction()

install_component(qa "${WORK_DIR}/qa")
install_component(Runtime "${WORK_DIR}/runtime")
foreach(tool IN ITEMS ayther_replay_qa ayther_audio_qa)
    if(NOT EXISTS "${WORK_DIR}/qa/bin/${tool}${CMAKE_EXECUTABLE_SUFFIX_CXX}" AND
       NOT EXISTS "${WORK_DIR}/qa/bin/${tool}.exe" AND NOT EXISTS "${WORK_DIR}/qa/bin/${tool}")
        message(FATAL_ERROR "the qa component does not contain ${tool}")
    endif()
    file(GLOB in_runtime "${WORK_DIR}/runtime/bin/${tool}*")
    if(in_runtime)
        message(FATAL_ERROR "the Runtime component contains ${tool}: ${in_runtime}")
    endif()
endforeach()
if(NOT EXISTS "${WORK_DIR}/runtime/bin/ayther_runtime.exe" AND
   NOT EXISTS "${WORK_DIR}/runtime/bin/ayther_runtime")
    message(FATAL_ERROR "the Runtime component lost ayther_runtime")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "qa component holds the QA tools and Runtime does not")
