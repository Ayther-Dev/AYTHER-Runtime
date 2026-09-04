execute_process(
    COMMAND "${RUNTIME_EXE}" --play-protocol-version 2
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 65)
    message(FATAL_ERROR "future Play protocol must exit 65; got ${result}")
endif()

execute_process(
    COMMAND "${RUNTIME_EXE}" --play-protocol-version 1
    RESULT_VARIABLE current_result
    OUTPUT_VARIABLE current_stdout
    ERROR_VARIABLE current_stderr
)
if(current_result EQUAL 65 OR
   current_stdout MATCHES "protocol.incompatible" OR
   current_stderr MATCHES "protocol.incompatible")
    message(FATAL_ERROR
        "current Play protocol must pass negotiation:\n"
        "${current_stdout}\n${current_stderr}")
endif()
string(FIND "${stdout}" "\"protocol_version\":1" has_version)
string(FIND "${stdout}" "\"reason\":\"protocol.incompatible\"" has_reason)
if(has_version EQUAL -1 OR has_reason EQUAL -1)
    message(FATAL_ERROR
        "incompatible protocol response is not machine-readable:\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${RUNTIME_EXE}" --play-protocol-version 0
    RESULT_VARIABLE old_result
    OUTPUT_VARIABLE old_stdout
    ERROR_VARIABLE old_stderr
)
if(NOT old_result EQUAL 65)
    message(FATAL_ERROR
        "unsupported older Play protocol must exit 65 before startup; "
        "got ${old_result}\n${old_stdout}\n${old_stderr}")
endif()
string(FIND "${old_stdout}" "\"protocol_version\":1" old_has_version)
string(FIND "${old_stdout}" "\"reason\":\"protocol.incompatible\"" old_has_reason)
if(old_has_version EQUAL -1 OR old_has_reason EQUAL -1)
    message(FATAL_ERROR
        "unsupported older protocol response is not machine-readable:\n"
        "${old_stdout}\n${old_stderr}")
endif()
