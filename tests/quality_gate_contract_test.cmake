cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED RUNTIME_SOURCE_DIR)
    message(FATAL_ERROR "RUNTIME_SOURCE_DIR is required")
endif()

file(READ "${RUNTIME_SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${RUNTIME_SOURCE_DIR}/tests/CMakeLists.txt" tests_cmake_text)
file(READ "${RUNTIME_SOURCE_DIR}/tests/audio_qa/CMakeLists.txt"
    audio_qa_cmake_text)
string(APPEND cmake_text "\n${tests_cmake_text}\n${audio_qa_cmake_text}")
file(READ "${RUNTIME_SOURCE_DIR}/CMakePresets.json" presets_text)
file(READ "${RUNTIME_SOURCE_DIR}/.clang-format" format_text)
file(READ "${RUNTIME_SOURCE_DIR}/.clang-tidy" tidy_text)
file(READ "${RUNTIME_SOURCE_DIR}/.github/workflows/ci.yml" ci_text)

foreach(token IN ITEMS
        "/W4" "/WX" "/permissive-" "/EHsc"
        "-Wall" "-Wextra" "-Wpedantic" "-Werror")
    string(FIND "${cmake_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "strict warning contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "BasedOnStyle: LLVM" "IndentWidth: 4" "ColumnLimit: 100")
    string(FIND "${format_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "clang-format contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "set(CMAKE_CXX_CLANG_TIDY" "audio_qa_build_targets"
        "run_clang_format_changed.sh" "Check modified C++ formatting")
    string(FIND "${cmake_text}\n${ci_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "audio QA quality contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "AYTHER_ENABLE_CLANG_TIDY" "AYTHER_ENABLE_COVERAGE"
        "AYTHER_ENABLE_SANITIZERS" "AYTHER_ENABLE_FUZZING")
    string(FIND "${presets_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "CI preset contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "WarningsAsErrors: '*'" "bugprone-use-after-move"
        "performance-unnecessary-copy-initialization")
    string(FIND "${tidy_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "clang-tidy contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "-fsanitize=address,undefined" "-fsanitize=fuzzer"
        "-runs=1000" "-max_len=4096")
    string(FIND "${cmake_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "sanitizer/fuzzer contract omits '${token}'")
    endif()
endforeach()

foreach(token IN ITEMS
        "--html-details artifacts/coverage.html"
        "--xml artifacts/coverage.xml"
        "--fail-under-line 60"
        "save_state_store"
        "vulkan_backend/spirv_file")
    string(FIND "${ci_text}" "${token}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "coverage contract omits '${token}'")
    endif()
endforeach()

file(GLOB_RECURSE first_party_sources
    "${RUNTIME_SOURCE_DIR}/src/*.cpp"
    "${RUNTIME_SOURCE_DIR}/src/*.h"
    "${RUNTIME_SOURCE_DIR}/tests/*.cpp"
    "${RUNTIME_SOURCE_DIR}/tests/*.h")
foreach(source IN LISTS first_party_sources)
    file(READ "${source}" source_text)
    if(source_text MATCHES "NOLINT")
        string(REGEX MATCHALL "NOLINT[^\r\n]*" suppressions "${source_text}")
        foreach(suppression IN LISTS suppressions)
            if(NOT suppression MATCHES "^NOLINT[A-Z]*\\([^\\)]+\\)")
                message(FATAL_ERROR
                    "${source}: NOLINT suppressions must include a local reason")
            endif()
        endforeach()
    endif()
endforeach()

message(STATUS "quality gates: strict warnings, tidy, sanitizers, fuzz and coverage are enforced")
