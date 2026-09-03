# ---------------------------------------------------------------------------
# runtime/tests — los oráculos del game-session host (ADR-004 §3, E8).
#
# Cada test compila fuentes de `runtime/src` y por eso vive ACÁ y no en el
# `tests/` de la raíz, que es del motor: un oráculo va con lo que prueba. El día
# del corte este directorio viaja entero con `ayther-runtime`, sin arreglos.
#
# Lo que necesitan del motor lo toman del paquete —`Ayther::engine`, que ya
# encontró runtime/CMakeLists.txt— (E8.1); nada de acá incluye `engine/src`.
# ---------------------------------------------------------------------------
find_program(AYTHER_PWSH_EXECUTABLE NAMES pwsh)
if(AYTHER_PWSH_EXECUTABLE)
    add_test(
        NAME ayther_engine_lock
        COMMAND ${AYTHER_PWSH_EXECUTABLE}
                -NoProfile
                -File ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bootstrap_ayther_engine.ps1
                -ValidateOnly
    )
    add_test(
        NAME vcpkg_manifest
        COMMAND ${AYTHER_PWSH_EXECUTABLE}
                -NoProfile
                -File ${CMAKE_CURRENT_SOURCE_DIR}/../tools/verify_vcpkg_manifest.ps1
                -Manifest ${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg.json
                -AytherConfig ${Ayther_DIR}/AytherConfig.cmake
    )
    add_test(
        NAME runtime_oot_smoke_contract
        COMMAND ${AYTHER_PWSH_EXECUTABLE}
                -NoProfile
                -File ${CMAKE_CURRENT_SOURCE_DIR}/runtime_oot_smoke_contract_test.ps1
                -SmokeScript ${CMAKE_CURRENT_SOURCE_DIR}/../tools/runtime_oot_smoke.ps1
    )
    add_test(
        NAME ci_workflow_contract
        COMMAND ${AYTHER_PWSH_EXECUTABLE}
                -NoProfile
                -File ${CMAKE_CURRENT_SOURCE_DIR}/ci_workflow_contract_test.ps1
                -Workflow ${CMAKE_CURRENT_SOURCE_DIR}/../.github/workflows/ci.yml
                -Presets ${CMAKE_CURRENT_SOURCE_DIR}/../CMakePresets.json
                -EngineLock ${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/ayther-engine.lock.json
    )
else()
    message(STATUS "pwsh not found; dependency manifest tests disabled")
endif()

# ------------------------------------------------------------------
# runtime_config_tests — MIG-020: Runtime owns its persistence paths.
#
# This target deliberately links neither SDL nor Engine. Path discovery and
# launcher override precedence must remain usable before either is initialized.
# ------------------------------------------------------------------
add_executable(runtime_config_tests
    runtime_config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/runtime_config.cpp
)
target_include_directories(runtime_config_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(runtime_config_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    runtime_config
    COMMAND runtime_config_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# output_profile_tests — MIG-021: Runtime-owned presentation policy.
#
# Profiles, scaling geometry, filtering, and shader mixing are pure window
# policy. This target links neither Engine nor Vulkan and must stay that way.
# ------------------------------------------------------------------
add_executable(output_profile_tests output_profile_test.cpp)
target_include_directories(output_profile_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(output_profile_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    output_profile
    COMMAND output_profile_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# player_config_tests — #299: la config del jugador, por juego+pack.
#
# El panel in-game necesita ventana, gamepad y Vulkan; esto fija lo
# que decide si la configuracion sirve — la clave, y la distincion
# entre «apago todo» y «nunca toco nada», que un 0 no puede codificar
# y que decide si el juego arranca remasterizado.
# ------------------------------------------------------------------
add_executable(player_config_tests
    player_config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/player_config.cpp
)
target_include_directories(player_config_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(player_config_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    player_config
    COMMAND player_config_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# split_geometry_tests — #298: donde cae cada mitad de la comparacion.
#
# Header-only y sin Vulkan: es la parte del split que se equivoca
# (mitades corridas, divisor invertido, regiones de ancho 0 que Vulkan
# rechaza y se ven como pantalla negra sin mensaje). Con la GPU adentro
# no se comprobaria nunca.
# ------------------------------------------------------------------
add_executable(split_geometry_tests split_geometry_test.cpp)
target_include_directories(split_geometry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(split_geometry_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    split_geometry
    COMMAND split_geometry_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# capture_tests — #300: la captura comparativa sincronizada.
#
# El «mismo frame logico» lo sostiene el diseño (las tres imagenes
# salen de un FrameView que export_frame no avanza), no un assert.
# Acá se fija lo que se equivoca: que pixel viene de cual, y que la
# metadata NO lleve material protegido.
#
# Este ejecutable es también el oráculo de enlace de MIG-030: no enlaza
# Ayther::engine, por lo que `capture.cpp` debe aportar stbi_write_png.
# ------------------------------------------------------------------
add_executable(capture_tests
    capture_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/capture.cpp
)
target_include_directories(capture_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(capture_tests PRIVATE ayther_runtime_stb)
set_target_properties(capture_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    capture
    COMMAND capture_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# pack_layers_tests — #561: el runtime arma el stack de Acetatos del pack.
#
# El defecto no era un calculo: era que NADIE llamaba. Por eso el primer
# caso es el del defecto (el stack por defecto no tiene ni una Custom) y
# el determinismo del flicker/animacion se llama fuera de orden.
# ------------------------------------------------------------------
add_executable(pack_layers_tests
    pack_layers_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/pack_layers.cpp
)
target_include_directories(pack_layers_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(pack_layers_tests PRIVATE Ayther::engine)
set_target_properties(pack_layers_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    pack_layers
    COMMAND pack_layers_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# diagnostics_tests — #302: el asistente de diagnostico.
#
# La MEDICION necesita SDL y una GPU; la DECISION es pura y es la que
# se equivoca — un orden de reglas mal puesto recomienda CRT en un
# equipo que no llega, o pixel-perfect donde la imagen queda diminuta.
# ------------------------------------------------------------------
add_executable(diagnostics_tests
    diagnostics_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostics.cpp
)
target_include_directories(diagnostics_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(diagnostics_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    diagnostics
    COMMAND diagnostics_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(sonic_telemetry_tests
    sonic_telemetry_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sonic_telemetry.cpp
)
target_include_directories(sonic_telemetry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(sonic_telemetry_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    sonic_telemetry
    COMMAND sonic_telemetry_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# version_info_tests — MIG-001: versiones de Runtime y Engine.
#
# Runtime sale de la versión de proyecto configurada por CMake; Engine se
# consulta al artefacto enlazado. Este oráculo evita que UI o logs vuelvan a
# fijar a mano una versión histórica.
# ------------------------------------------------------------------
add_executable(version_info_tests version_info_test.cpp)
target_include_directories(version_info_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${AYTHER_RUNTIME_GENERATED_DIR}
)
target_link_libraries(version_info_tests PRIVATE Ayther::engine)
set_target_properties(version_info_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    version_info
    COMMAND version_info_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# core_probe_api_tests — MIG-022: installed public Engine probe facade.
#
# This test remains buildable while other private-header migrations keep the
# complete Runtime executable from compiling against the narrowed package.
# ------------------------------------------------------------------
add_executable(core_probe_api_tests core_probe_api_test.cpp)
target_link_libraries(core_probe_api_tests PRIVATE Ayther::engine)
target_compile_definitions(core_probe_api_tests PRIVATE
    AYTHER_MISSING_CORE_PATH="${CMAKE_CURRENT_BINARY_DIR}/missing-core")
set_target_properties(core_probe_api_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    core_probe_api
    COMMAND core_probe_api_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# pack_api_contract_tests — MIG-027: typed core/pack/watcher boundary.
#
# Compiles only against the installed Engine target. No raw C FFI header or
# opaque core handle is visible to this Runtime consumer.
# ------------------------------------------------------------------
add_executable(pack_api_contract_tests pack_api_contract_test.cpp)
target_link_libraries(pack_api_contract_tests PRIVATE Ayther::engine)
set_target_properties(pack_api_contract_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    pack_api_contract
    COMMAND pack_api_contract_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# input_api_contract_tests — public normalized joypad boundary.
#
# The Runtime maps SDL state to this value type and does not include Engine's
# Libretro implementation header.
# ------------------------------------------------------------------
add_executable(input_api_contract_tests
    input_api_contract_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/game_input.cpp
)
target_include_directories(input_api_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(input_api_contract_tests PRIVATE
    Ayther::engine
    SDL3::SDL3
)
set_target_properties(input_api_contract_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    input_api_contract
    COMMAND input_api_contract_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# render_image_view_api_tests — MIG-024: public Vulkan image handoff.
#
# It compiles only against the installed Ayther::engine target so Vulkan's
# public include/link requirements and the value contract are both exercised.
# ------------------------------------------------------------------
add_executable(render_image_view_api_tests render_image_view_api_test.cpp)
target_link_libraries(render_image_view_api_tests PRIVATE Ayther::engine)
set_target_properties(render_image_view_api_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    render_image_view_api
    COMMAND render_image_view_api_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# vulkan_context_tests — MIG-025: Runtime owns Vulkan device lifetime.
#
# Its default mode needs no window or GPU. It compiles the real implementation,
# checks the ownership type, and exercises empty/idempotent teardown. The
# explicit `--gpu` mode also validates real SDL surface/device creation.
# ------------------------------------------------------------------
add_executable(vulkan_context_tests
    vulkan_context_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend/vk_context.cpp
)
target_include_directories(vulkan_context_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(vulkan_context_tests PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=1
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
target_link_libraries(vulkan_context_tests PRIVATE
    Ayther::engine
    SDL3::SDL3
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    vk-bootstrap::vk-bootstrap
)
set_target_properties(vulkan_context_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    vulkan_context
    COMMAND vulkan_context_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# vulkan_presentation_contract_tests — MIG-025 integration boundary.
#
# Compile and link Runtime's swapchain, present, postprocess, and ImGui overlay
# without main.cpp or any Engine-private include path.
# ------------------------------------------------------------------
add_executable(vulkan_presentation_contract_tests
    vulkan_presentation_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/player_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend/vk_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend/vk_swapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend/vk_present.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend/vk_postprocess.cpp
)
target_include_directories(vulkan_presentation_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vulkan_backend
)
target_compile_definitions(vulkan_presentation_contract_tests PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=1
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
target_link_libraries(vulkan_presentation_contract_tests PRIVATE
    Ayther::engine
    SDL3::SDL3
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    vk-bootstrap::vk-bootstrap
    imgui::imgui
)
set_target_properties(vulkan_presentation_contract_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(
    NAME    vulkan_presentation_contract
    COMMAND vulkan_presentation_contract_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ------------------------------------------------------------------
# probe_core — #589: el sondeo de cores que consume Play.
#
# Corre el binario del runtime en vez de compilar fuentes: lo que se
# prueba es el CONTRATO de linea de comandos (codigo de salida + una
# linea AYTHER_STATUS), que es exactamente lo que Play ve. Un test que
# llamara a CoreProbe directo pasaria en verde con el contrato roto.
#
# El nombre de la DLL sale de core.lock para no repetirlo: el dia que
# el fork cambie de version, cambia en un solo lugar.
# ------------------------------------------------------------------
set(_core_dll "")
set(_lock "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cores/core.lock")
if(EXISTS "${_lock}")
    file(READ "${_lock}" _lock_txt)
    if(_lock_txt MATCHES "vram_file[ \t]*=[ \t]*([^\n\r]+)")
        string(STRIP "${CMAKE_MATCH_1}" _core_name)
        set(_core_dll "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cores/${_core_name}")
    endif()
endif()
add_test(
    NAME    probe_core
    COMMAND ${CMAKE_COMMAND}
            -DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>
            -DCORE_DLL=${_core_dll}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/probe_core_test.cmake
)
