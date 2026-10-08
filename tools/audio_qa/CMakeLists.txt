find_package(tomlplusplus 3.4.0 EXACT CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

add_library(ayther_audio_qa_domain STATIC reference_model.cpp runtime_reference_comparison.cpp
    content_hash.cpp fact_model.cpp fact_trace_summary.cpp mix_model.cpp audio_chunk.cpp
    inspection_fact_builder.cpp
    material_consumption_guard.cpp
    initial_state.cpp coverage_model.cpp capabilities.cpp protocol_header.cpp recording_header.cpp
    replay_duration_limit.cpp
    trace_query.cpp coexistence_query.cpp
    recording_input_source.cpp recording_replay_preparation.cpp replay_start_policy.cpp
    hd_initialization.cpp initial_state_publication.cpp
    recording_replay_loop.cpp replay_control_gate.cpp
    replay_condition_lock.cpp replay_progress.cpp
    replay_production_close.cpp run_lifecycle.cpp session_occupancy.cpp
    request_registry.cpp take_run_sequence.cpp take_process_sequence.cpp
    replay_cancellation.cpp
    replay_terminal_arbiter.cpp evidence_close.cpp process_cessation.cpp
    evidence_quota.cpp
    capability_gate.cpp capability_report.cpp progress_watchdog.cpp
    cancellation_window.cpp)
target_include_directories(ayther_audio_qa_domain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_domain PUBLIC cxx_std_20)
target_link_libraries(ayther_audio_qa_domain PRIVATE zstd::libzstd)
set_target_properties(ayther_audio_qa_domain PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_domain PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_domain PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_domain PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ayther_audio_qa_materials STATIC material_staging_budget.cpp
    pinned_take.cpp pinned_pack.cpp pinned_game_materials.cpp)
target_include_directories(ayther_audio_qa_materials PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_materials PUBLIC cxx_std_20)
target_link_libraries(ayther_audio_qa_materials PUBLIC ayther_audio_qa_domain)
set_target_properties(ayther_audio_qa_materials PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_materials PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

add_library(ayther_audio_qa_preparation STATIC isolated_saves.cpp
    isolated_runtime_data.cpp)
target_include_directories(ayther_audio_qa_preparation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_preparation PUBLIC cxx_std_20)
target_link_libraries(ayther_audio_qa_preparation PUBLIC ayther_audio_qa_domain)
set_target_properties(ayther_audio_qa_preparation PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_preparation PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_preparation PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_preparation PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ayther_audio_qa_evidence STATIC
    audio_integrity.cpp checkpoint_store.cpp checkpoint_threshold.cpp durable_file.cpp
    exclusive_evidence_directory.cpp
    exclusive_file.cpp fact_fragment_store.cpp fact_integrity.cpp pcm_block_store.cpp
    reference_store.cpp request_ledger.cpp run_recovery.cpp wav_derivation.cpp)
target_include_directories(ayther_audio_qa_evidence PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_evidence PUBLIC cxx_std_20)
target_link_libraries(ayther_audio_qa_evidence PUBLIC ayther_audio_qa_domain
    PRIVATE tomlplusplus::tomlplusplus)
set_target_properties(ayther_audio_qa_evidence PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_evidence PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

add_executable(ayther_audio_qa_wav_export wav_export_main.cpp)
target_link_libraries(ayther_audio_qa_wav_export PRIVATE ayther_audio_qa_evidence)
target_compile_features(ayther_audio_qa_wav_export PRIVATE cxx_std_20)
set_target_properties(ayther_audio_qa_wav_export PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(ayther_audio_qa_wav_export PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_wav_export PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_evidence PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_evidence PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

if(MSVC)
    target_compile_options(ayther_audio_qa_materials PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_materials PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ayther_audio_qa_model_toml STATIC model_toml.cpp control_message.cpp
    replay_execution_result.cpp
    fact_batch.cpp pcm_message.cpp cancellation_message.cpp
    play_launch_manifest.cpp session_status_message.cpp)
target_link_libraries(ayther_audio_qa_model_toml PUBLIC ayther_audio_qa_domain
    PRIVATE tomlplusplus::tomlplusplus)
target_link_libraries(ayther_audio_qa_evidence PRIVATE
    ayther_audio_qa_model_toml)
set_target_properties(ayther_audio_qa_model_toml PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_model_toml PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

add_library(ayther_audio_qa_transport STATIC inherited_channel.cpp protocol_io.cpp
    runtime_process.cpp separated_channel_drain.cpp channel_pump.cpp)
target_include_directories(ayther_audio_qa_transport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_transport PUBLIC cxx_std_20)
set_target_properties(ayther_audio_qa_transport PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_transport PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

if(AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE)
    add_library(ayther_audio_qa_observation_bridge INTERFACE)
    target_include_directories(ayther_audio_qa_observation_bridge INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ayther_audio_qa_observation_bridge INTERFACE
        cxx_std_20)
    target_link_libraries(ayther_audio_qa_observation_bridge INTERFACE
        Ayther::engine)

    add_library(ayther_audio_qa_engine_replay STATIC
        engine_game_state_restore.cpp engine_hd_initialization.cpp
        engine_recording_replay.cpp engine_replay_production_close.cpp
        replay_trace.cpp)
    target_include_directories(ayther_audio_qa_engine_replay PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(ayther_audio_qa_engine_replay PUBLIC cxx_std_20)
    target_link_libraries(ayther_audio_qa_engine_replay PUBLIC
        ayther_audio_qa_domain Ayther::engine PRIVATE
        ayther_audio_qa_model_toml)
    if(TARGET Ayther::vpx)
        target_link_libraries(ayther_audio_qa_engine_replay PRIVATE
            Ayther::vpx)
    endif()
    set_target_properties(ayther_audio_qa_engine_replay PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(ayther_audio_qa_engine_replay PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(ayther_audio_qa_engine_replay PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(ayther_audio_qa_engine_replay PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_transport PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_transport PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_model_toml PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_model_toml PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ayther_audio_qa_cli STATIC check_options.cpp check_option_descriptors.cpp
    effective_values.cpp json_fields.cpp material_preflight.cpp request_outcome.cpp
    request_preflight.cpp request_summary_store.cpp request_terminal_arbiter.cpp
    runtime_protocol_v11.cpp inspection_evidence.cpp inspection_facts.cpp
    evidence_completeness.cpp
    runtime_probe.cpp take_plan.cpp check_runner.cpp
    check_admission.cpp check_cancellation.cpp check_execution.cpp
    check_messages.cpp check_summary.cpp integrated_evidence.cpp incremental_evidence.cpp
    campaign_audit.cpp console_interrupt.cpp evidence_query_index.cpp query_options.cpp)
target_include_directories(ayther_audio_qa_cli PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_audio_qa_cli PUBLIC cxx_std_20)
target_link_libraries(ayther_audio_qa_cli PUBLIC
    ayther_audio_qa_domain ayther_audio_qa_evidence
    PRIVATE ayther_audio_qa_model_toml ayther_audio_qa_transport
        ayther_audio_qa_preparation
        tomlplusplus::tomlplusplus)
set_target_properties(ayther_audio_qa_cli PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa_cli PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa_cli PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa_cli PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(ayther_audio_qa check_main.cpp)
target_compile_features(ayther_audio_qa PRIVATE cxx_std_20)
if(DEFINED AYTHER_RUNTIME_GENERATED_DIR)
    # Spec 002 (RF-1.8): `options --format toml` names the compiled Runtime version.
    target_include_directories(ayther_audio_qa PRIVATE "${AYTHER_RUNTIME_GENERATED_DIR}")
    target_compile_definitions(ayther_audio_qa PRIVATE AYTHER_AUDIO_QA_HAS_RUNTIME_VERSION=1)
endif()
if(TARGET ayther_runtime_source_commit)
    # Spec 002 (C5, RF-1.8): and the commit of its sources.
    add_dependencies(ayther_audio_qa ayther_runtime_source_commit)
    target_compile_definitions(ayther_audio_qa PRIVATE AYTHER_AUDIO_QA_HAS_SOURCE_COMMIT=1)
endif()
target_link_libraries(ayther_audio_qa PRIVATE
    ayther_audio_qa_domain
    ayther_audio_qa_materials
    ayther_audio_qa_preparation
    ayther_audio_qa_evidence
    ayther_audio_qa_model_toml
    ayther_audio_qa_transport
    ayther_audio_qa_cli)
set_target_properties(ayther_audio_qa PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(ayther_audio_qa PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(ayther_audio_qa PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(ayther_audio_qa PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    find_file(ayther_audio_qa_toml_dll_release NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../bin" NO_DEFAULT_PATH REQUIRED)
    find_file(ayther_audio_qa_toml_dll_debug NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../debug/bin" NO_DEFAULT_PATH REQUIRED)
    add_custom_command(TARGET ayther_audio_qa POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${ayther_audio_qa_toml_dll_debug},${ayther_audio_qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:ayther_audio_qa>"
        VERBATIM)
endif()
