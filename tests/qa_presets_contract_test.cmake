cmake_minimum_required(VERSION 3.25)

# Spec 002, BR-118 (RNF-8): the QA presets configure, build and test against the QA
# Engine lock; `windows-qa-gpu` adds the GPU tests. The presets read the prefix that
# tools/bootstrap_ayther_engine_qa.ps1 prints, through AYTHER_ENGINE_PREFIX.
if(NOT DEFINED PRESETS OR NOT EXISTS "${PRESETS}")
    message(FATAL_ERROR "PRESETS is required")
endif()
file(READ "${PRESETS}" presets)

function(find_preset kind name out_index)
    string(JSON count LENGTH "${presets}" ${kind})
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON candidate GET "${presets}" ${kind} ${index} name)
        if(candidate STREQUAL name)
            set(${out_index} ${index} PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "${kind} has no preset ${name}")
endfunction()

function(expect_cache name variable expected)
    find_preset(configurePresets ${name} index)
    string(JSON value ERROR_VARIABLE error GET "${presets}" configurePresets ${index}
           cacheVariables ${variable})
    if(NOT error STREQUAL "NOTFOUND" OR NOT value STREQUAL expected)
        message(FATAL_ERROR "${name}: ${variable} is '${value}' (${error}), expected ${expected}")
    endif()
endfunction()

foreach(name IN ITEMS windows-qa windows-qa-gpu)
    find_preset(configurePresets ${name} configure)
    find_preset(buildPresets ${name} build)
    find_preset(testPresets ${name} test)
    string(JSON inherits GET "${presets}" configurePresets ${configure} inherits)
    if(NOT inherits MATCHES "windows-ci|windows-qa")
        message(FATAL_ERROR "${name} does not inherit the Windows toolchain: ${inherits}")
    endif()
    expect_cache(${name} AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE ON)
endforeach()
expect_cache(windows-qa AYTHER_ENABLE_GPU_TESTS OFF)
expect_cache(windows-qa-gpu AYTHER_ENABLE_GPU_TESTS ON)

# Without GPU the QA preset leaves the GPU label out; the GPU preset runs it.
find_preset(testPresets windows-qa test)
string(JSON excluded ERROR_VARIABLE error GET "${presets}" testPresets ${test} filter exclude label)
if(NOT error STREQUAL "NOTFOUND" OR NOT excluded STREQUAL "gpu")
    message(FATAL_ERROR "windows-qa does not exclude the gpu label: ${excluded} ${error}")
endif()
message(STATUS "QA presets declared")
