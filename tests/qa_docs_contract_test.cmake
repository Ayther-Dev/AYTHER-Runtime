cmake_minimum_required(VERSION 3.25)

# Spec 002, BR-176 and BR-177 (RNF-7, RNF-8): the replay QA documentation exists and says what
# the code does. Each check names the text the guide must contain or must no longer contain.
if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

function(read_doc out relative)
    set(path "${SOURCE_ROOT}/${relative}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${relative} is missing")
    endif()
    file(READ "${path}" contents)
    set(${out} "${contents}" PARENT_SCOPE)
endfunction()

function(require document relative)
    foreach(text IN LISTS ARGN)
        string(FIND "${document}" "${text}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${relative} does not mention '${text}'")
        endif()
    endforeach()
endfunction()

# BR-176: the launcher guide, in Spanish, with use, build and tests.
read_doc(launcher "tools/replay_qa_launcher/README.md")
require("${launcher}" "tools/replay_qa_launcher/README.md"
    "## Uso" "## Compilación" "## Pruebas" "ayther_replay_qa" "Iniciar" "Cancelar"
    "Sin pack" "--language" "es" "en" "--smoke-frames" "--self-test"
    "replay_qa_launcher_interface_smoke" "sdl3-renderer-binding")

# BR-176: the QA commands in the development guide.
read_doc(development "docs/development.md")
require("${development}" "docs/development.md"
    "## Replay QA" "tools/bootstrap_ayther_engine_qa.ps1" "cmake --preset windows-qa"
    "cmake --build --preset windows-qa" "ctest --preset windows-qa -L audio_qa -LE gpu"
    "windows-qa-gpu" "sdl3-renderer-binding" "AYTHER_QA_INPUT_SCRIPT"
    "AYTHER_QA_TIMING_LOG" "ayther_replay_qa")

# BR-177: traversal, visits and summary, and the corrected pinning of materials.
read_doc(evidence "docs/audio-qa-evidence.md")
require("${evidence}" "docs/audio-qa-evidence.md"
    "traversal.toml" "request-summary.toml" "visita" "post_end_inspection" "inspection_event"
    "render_frame" "material_changed" "FILE_SHARE_READ" "Espacio" "replay-result.toml")
string(FIND "${evidence}" "Antes de iniciar, el comprobador fija las identidades" stale)
if(NOT stale EQUAL -1)
    message(FATAL_ERROR "docs/audio-qa-evidence.md still claims the old pinning of materials")
endif()

# BR-177: Space, the arrows and I in the replay window.
read_doc(input_map "docs/input-map.md")
require("${input_map}" "docs/input-map.md"
    "## Replay QA window" "Space" "Left" "Right" "`I`" "Page Up" "never reach the game")

message(STATUS "the replay QA documentation covers the launcher, the commands and the controls")
