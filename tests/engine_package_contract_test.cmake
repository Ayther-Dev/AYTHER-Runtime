cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CONTRACT_MODULE)
    message(FATAL_ERROR "CONTRACT_MODULE is required")
endif()
include("${CONTRACT_MODULE}")

function(expect_compatible label)
    _ayther_evaluate_engine_package_contract(_ok _message ${ARGN})
    if(NOT _ok)
        message(FATAL_ERROR "${label}: expected compatible:\n${_message}")
    endif()
endfunction()

function(expect_rejected label expected)
    _ayther_evaluate_engine_package_contract(_ok _message ${ARGN})
    if(_ok OR NOT _message MATCHES "${expected}")
        message(FATAL_ERROR
            "${label}: expected rejection matching '${expected}':\n${_message}")
    endif()
    foreach(field IN ITEMS "Found package=" "Required AYTHER Engine package:"
                           "Use the locked v0.1.0-rc.6 archive")
        if(NOT _message MATCHES "${field}")
            message(FATAL_ERROR "${label}: diagnostic omitted '${field}'")
        endif()
    endforeach()
endfunction()

set(common MIN_VERSION 0.1.0 MAX_VERSION_EXCLUSIVE 0.2.0
           SYSTEM_PROCESSOR x86_64 POINTER_SIZE 8)

expect_compatible("Windows v145 package"
    PACKAGE_VERSION 0.1.0 SYSTEM_NAME Windows
    COMPILER_ID MSVC COMPILER_VERSION 19.51 ${common})
expect_compatible("Ubuntu Clang 18 package"
    PACKAGE_VERSION 0.1.0 SYSTEM_NAME Linux
    COMPILER_ID Clang COMPILER_VERSION 18.1.3 ${common})

expect_rejected("package too old" "outside the supported range"
    PACKAGE_VERSION 0.0.9 SYSTEM_NAME Linux
    COMPILER_ID Clang COMPILER_VERSION 18.1.3 ${common})
expect_rejected("package too new" "outside the supported range"
    PACKAGE_VERSION 0.2.0 SYSTEM_NAME Linux
    COMPILER_ID Clang COMPILER_VERSION 18.1.3 ${common})
expect_rejected("wrong architecture" "architecture is incompatible"
    PACKAGE_VERSION 0.1.0 SYSTEM_NAME Linux SYSTEM_PROCESSOR aarch64
    POINTER_SIZE 8 COMPILER_ID Clang COMPILER_VERSION 18.1.3
    MIN_VERSION 0.1.0 MAX_VERSION_EXCLUSIVE 0.2.0)
expect_rejected("wrong Linux compiler" "Clang 18 ABI baseline"
    PACKAGE_VERSION 0.1.0 SYSTEM_NAME Linux
    COMPILER_ID GNU COMPILER_VERSION 14.2 ${common})
expect_rejected("future Linux compiler" "Clang 18 ABI baseline"
    PACKAGE_VERSION 0.1.0 SYSTEM_NAME Linux
    COMPILER_ID Clang COMPILER_VERSION 19.0 ${common})

message(STATUS "Engine package compatibility cases passed")
