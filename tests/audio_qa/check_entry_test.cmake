if(NOT DEFINED CHECK_EXE OR CHECK_EXE STREQUAL "")
    message(FATAL_ERROR "CHECK_EXE is required")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
    message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(assert_invocation expected_result expected_pattern)
    execute_process(
        COMMAND "${CHECK_EXE}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    if(NOT actual_result EQUAL expected_result)
        message(FATAL_ERROR
            "Unexpected result ${actual_result}; expected ${expected_result}. "
            "stdout='${standard_output}' stderr='${standard_error}'")
    endif()
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    if(NOT combined_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "Missing diagnostic '${expected_pattern}' in '${combined_output}'")
    endif()
endfunction()

# Spec 002 (RNF-7): the localized message follows --language, also for errors
# detected before the request is admitted.
function(assert_localized_invocation expected_language output_root request_id)
    set(language_arguments)
    if(expected_language STREQUAL "en")
        list(APPEND language_arguments --language en)
    endif()
    execute_process(
        COMMAND "${CHECK_EXE}" check
            --runtime runtime.exe
            --rom game.md
            --take main.ayr
            --play-manifest play.toml
            --output "${output_root}"
            --request-id "${request_id}"
            ${language_arguments}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    if(NOT actual_result EQUAL 3)
        message(FATAL_ERROR
            "Unexpected localized result ${actual_result}; expected 3. "
            "stdout='${standard_output}' stderr='${standard_error}'")
    endif()
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    foreach(expected_pattern IN ITEMS
            "play_manifest_unavailable"
            "audio_qa_message\\[${expected_language}\\]")
        if(NOT combined_output MATCHES "${expected_pattern}")
            message(FATAL_ERROR
                "Missing localized diagnostic '${expected_pattern}' in "
                "'${combined_output}'")
        endif()
    endforeach()
    if(EXISTS "${output_root}")
        message(FATAL_ERROR "A rejected request created its destination: ${output_root}")
    endif()
endfunction()

assert_invocation(3 "invalid_invocation" )
assert_invocation(3 "audio_qa_usage: ayther_audio_qa check" )
assert_invocation(3 "invalid_invocation" inspect)
assert_invocation(3 "missing_required_option: --runtime" check)
# RF-1.1: the ROM is an explicit selection.
assert_invocation(3 "missing_required_option: --rom" check --runtime runtime.exe)
# RF-1.2: no default take; the message follows --language (RNF-7).
assert_invocation(3 "missing_required_option: --take.*audio_qa_message\\[es\\]" check
    --runtime runtime.exe --rom game.md --output "${TEST_ROOT}")
assert_invocation(3 "missing_required_option: --take.*audio_qa_message\\[en\\]" check
    --runtime runtime.exe --rom game.md --output "${TEST_ROOT}" --language en)

# Spec 002 (RF-1.2, RF-2.2, plan §5.2): every material is validated before admission.
# Each one names its field, the effective values are shown, the exit is 3, and nothing
# is created in the destination, whether it exists or not.
file(WRITE "${TEST_ROOT}/existing/marker.txt" "untouched")
file(GLOB_RECURSE tree_before LIST_DIRECTORIES true "${TEST_ROOT}/existing/*")
foreach(destination IN ITEMS "${TEST_ROOT}/existing" "${TEST_ROOT}/never-created")
    foreach(expected IN ITEMS
            "audio_qa_effective: pack=none source=explicit"
            "material_not_found: --runtime"
            "material_not_found: --rom"
            "material_not_found: --core"
            "material_not_found: --take\\[0\\]"
            "material_not_found: --take\\[1\\]")
        assert_invocation(3 "${expected}" check
            --runtime runtime.exe --rom game.md --core core.dll --take main.ayr
            --take main.ayr --output "${destination}" --request-id request-entry)
    endforeach()
endforeach()
file(GLOB_RECURSE tree_after LIST_DIRECTORIES true "${TEST_ROOT}/existing/*")
if(NOT tree_before STREQUAL tree_after)
    message(FATAL_ERROR
        "A rejected request changed the destination:\n${tree_before}\n${tree_after}")
endif()
file(READ "${TEST_ROOT}/existing/marker.txt" marker)
if(EXISTS "${TEST_ROOT}/never-created" OR NOT marker STREQUAL "untouched")
    message(FATAL_ERROR "A rejected request wrote in its destination")
endif()
# RNF-7 (BR-071): each issue has its localized text, in the language of the request.
assert_invocation(3 "audio_qa_message\\[en\\]: --rom: The file does not exist" check
    --runtime runtime.exe --rom game.md --core core.dll --take main.ayr
    --output "${TEST_ROOT}/never-created" --language en)
assert_invocation(3 "audio_qa_message\\[es\\]: --rom: El archivo no existe" check
    --runtime runtime.exe --rom game.md --core core.dll --take main.ayr
    --output "${TEST_ROOT}/never-created")
assert_invocation(3 "audio_qa_message\\[en\\]: --take: A required option is missing" check
    --runtime runtime.exe --rom game.md --output "${TEST_ROOT}/never-created" --language en)
# A directory is not a material, in any field.
assert_invocation(3 "material_is_directory: --rom" check
    --runtime runtime.exe --rom "${TEST_ROOT}/existing" --core core.dll --take main.ayr
    --output "${TEST_ROOT}/never-created")
# An unreadable auxiliary source is rejected before admission.
assert_invocation(3 "play_manifest_unavailable" check
    --runtime runtime.exe --rom game.md --take main.ayr --play-manifest play.toml
    --output "${TEST_ROOT}/never-created" --request-id request-other)

assert_localized_invocation(es "${TEST_ROOT}/es" request-es)
assert_localized_invocation(en "${TEST_ROOT}/en" request-en)

file(REMOVE_RECURSE "${TEST_ROOT}")
