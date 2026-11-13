# ---------------------------------------------------------------------------
# probe_core — launcher-facing core metadata and failure contract.
#
# The positive fixture is built in this repository and exposes only the two
# Libretro symbols consumed by Engine's CoreProbe (rc.6 onward). The negative library is
# loadable but deliberately exports neither symbol. No downloaded core or
# external lock participates in this test.
# ---------------------------------------------------------------------------

foreach(required IN ITEMS RUNTIME_EXE CORE_DLL NON_CORE_DLL ROM_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "falta el argumento requerido ${required}")
    endif()
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "no existe ${required}: ${${required}}")
    endif()
endforeach()

set(CASOS_CORRIDOS 0)

function(sondear etiqueta objetivo esperado_codigo salida_json)
    execute_process(
        COMMAND "${RUNTIME_EXE}" --probe-core "${objetivo}" ${ARGN}
        OUTPUT_VARIABLE salida
        ERROR_VARIABLE err
        RESULT_VARIABLE codigo
        TIMEOUT 60
    )
    if(NOT codigo EQUAL esperado_codigo)
        message(FATAL_ERROR
            "[${etiqueta}] el sondeo devolvió ${codigo}, se esperaba ${esperado_codigo}.\n"
            "--- stdout ---\n${salida}\n--- stderr ---\n${err}")
    endif()

    set(prefijo "AYTHER_STATUS ")
    string(FIND "${salida}" "${prefijo}" status_at)
    if(status_at EQUAL -1)
        message(FATAL_ERROR
            "[${etiqueta}] falta la línea AYTHER_STATUS.\n--- stdout ---\n${salida}")
    endif()
    string(LENGTH "${prefijo}" longitud_prefijo)
    math(EXPR json_at "${status_at} + ${longitud_prefijo}")
    string(SUBSTRING "${salida}" ${json_at} -1 cola_status)
    string(FIND "${cola_status}" "\n" fin_linea)
    if(fin_linea EQUAL -1)
        set(json "${cola_status}")
    else()
        string(SUBSTRING "${cola_status}" 0 ${fin_linea} json)
    endif()
    string(FIND "${cola_status}" "${prefijo}" segundo_status)
    if(NOT segundo_status EQUAL -1)
        message(FATAL_ERROR
            "[${etiqueta}] se emitió más de una línea AYTHER_STATUS.\n${salida}")
    endif()
    set(linea "${prefijo}${json}")
    string(JSON event ERROR_VARIABLE json_error GET "${json}" event)
    if(NOT json_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "[${etiqueta}] AYTHER_STATUS no contiene JSON válido: ${json_error}\n${linea}")
    endif()
    if(NOT event STREQUAL "probe")
        message(FATAL_ERROR "[${etiqueta}] el evento no es `probe`: ${json}")
    endif()

    math(EXPR n "${CASOS_CORRIDOS} + 1")
    set(CASOS_CORRIDOS ${n} PARENT_SCOPE)
    set(${salida_json} "${json}" PARENT_SCOPE)
    message(STATUS "[probe_core] ${etiqueta}: ${linea}")
endfunction()

function(exigir_campo etiqueta json campo tipo_esperado valor_esperado)
    string(JSON tipo ERROR_VARIABLE tipo_error TYPE "${json}" "${campo}")
    if(NOT tipo_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "[${etiqueta}] falta el campo JSON `${campo}`: ${tipo_error}\n${json}")
    endif()
    if(NOT tipo STREQUAL tipo_esperado)
        message(FATAL_ERROR
            "[${etiqueta}] `${campo}` tiene tipo ${tipo}; se esperaba ${tipo_esperado}")
    endif()

    string(JSON valor ERROR_VARIABLE valor_error GET "${json}" "${campo}")
    if(NOT valor_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "[${etiqueta}] no se pudo leer `${campo}`: ${valor_error}\n${json}")
    endif()
    if(NOT "${valor}" STREQUAL "${valor_esperado}")
        message(FATAL_ERROR
            "[${etiqueta}] `${campo}` vale `${valor}`; se esperaba `${valor_esperado}`")
    endif()
endfunction()

# Spec 008 (RF-9.3, TP-304.5): the line is the probe contract v2 of the Engine
# (`verdict`, `reason`, exit 0/2/3/4), plus `loaded` and the Libretro metadata.

# 1. Missing file: verdict missing_file (exit 2).
set(archivo_ausente "${CORE_DLL}.missing")
if(EXISTS "${archivo_ausente}")
    message(FATAL_ERROR "el fixture de archivo ausente existe: ${archivo_ausente}")
endif()
sondear("archivo ausente" "${archivo_ausente}" 2 json_ausente)
exigir_campo("archivo ausente" "${json_ausente}" "protocol_version" "NUMBER" "2")
exigir_campo("archivo ausente" "${json_ausente}" "ok" "BOOLEAN" "OFF")
exigir_campo("archivo ausente" "${json_ausente}" "verdict" "STRING" "missing_file")
exigir_campo("archivo ausente" "${json_ausente}" "reason" "STRING" "core.missing")
exigir_campo("archivo ausente" "${json_ausente}" "loaded" "BOOLEAN" "OFF")

# 2. Loadable shared library without the required Libretro symbols (exit 3).
sondear("no es un core" "${NON_CORE_DLL}" 3 json_no_core)
exigir_campo("no es un core" "${json_no_core}" "ok" "BOOLEAN" "OFF")
exigir_campo("no es un core" "${json_no_core}" "verdict" "STRING" "not_a_core")
exigir_campo("no es un core" "${json_no_core}" "reason" "STRING" "core.invalid")

# 3. Deterministic synthetic Libretro core without the AYTHER interface: the
#    contract says abi_missing (exit 4); the Libretro metadata is still there.
sondear("core sintético" "${CORE_DLL}" 4 json_core)
exigir_campo("core sintético" "${json_core}" "ok" "BOOLEAN" "OFF")
exigir_campo("core sintético" "${json_core}" "verdict" "STRING" "abi_missing")
exigir_campo("core sintético" "${json_core}" "reason" "STRING" "core.abi_missing")
exigir_campo("core sintético" "${json_core}" "loaded" "BOOLEAN" "ON")
exigir_campo("core sintético" "${json_core}" "api" "NUMBER" "1")
exigir_campo("core sintético" "${json_core}" "library_name" "STRING" "AYTHER Synthetic Core")
exigir_campo("core sintético" "${json_core}" "library_version" "STRING" "1.0-test")
exigir_campo("core sintético" "${json_core}" "valid_extensions" "STRING" "aytest|rom")
exigir_campo("core sintético" "${json_core}" "need_fullpath" "BOOLEAN" "OFF")
exigir_campo("core sintético" "${json_core}" "block_extract" "BOOLEAN" "OFF")

# 4. Spec 002 (contracts.md C5): the core also loads its ROM, without running a frame,
#    and reports the timing the replay will use.
sondear("core sintético con ROM" "${CORE_DLL}" 4 json_rom --probe-rom "${ROM_FILE}")
exigir_campo("core sintético con ROM" "${json_rom}" "loaded" "BOOLEAN" "ON")
exigir_campo("core sintético con ROM" "${json_rom}" "game_loaded" "BOOLEAN" "ON")
exigir_campo("core sintético con ROM" "${json_rom}" "timing_fps" "NUMBER" "60")
string(JSON tipo_geometria ERROR_VARIABLE geometria_error TYPE "${json_rom}" geometry_known)
if(NOT geometria_error STREQUAL "NOTFOUND" OR NOT tipo_geometria STREQUAL "BOOLEAN")
    message(FATAL_ERROR "[core sintético con ROM] falta geometry_known\n${json_rom}")
endif()

# 5. A ROM the core does not load: the probe answers with the Engine message.
get_filename_component(directorio_rom_vacia "${CORE_DLL}" DIRECTORY)
set(rom_vacia "${directorio_rom_vacia}/probe-empty.aytest")
file(WRITE "${rom_vacia}" "")
sondear("ROM rechazada" "${CORE_DLL}" 4 json_rechazo --probe-rom "${rom_vacia}")
file(REMOVE "${rom_vacia}")
exigir_campo("ROM rechazada" "${json_rechazo}" "loaded" "BOOLEAN" "ON")
exigir_campo("ROM rechazada" "${json_rechazo}" "game_loaded" "BOOLEAN" "OFF")
string(JSON fps_rechazo ERROR_VARIABLE fps_rechazo_error GET "${json_rechazo}" timing_fps)
if(fps_rechazo_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "[ROM rechazada] un juego no cargado no tiene timing\n${json_rechazo}")
endif()

# 6. The shared vectors of the Engine package (TP-013), in process. The
#    `timeout` vector is set by a supervising parent, not by the probe, and the
#    `external` vectors need material this repository does not ship.
set(casos_esperados 5)
if(DEFINED PROBE_VECTORS_DIR AND EXISTS "${PROBE_VECTORS_DIR}/probe-vectors.json")
    file(READ "${PROBE_VECTORS_DIR}/probe-vectors.json" vectores)
    string(JSON n_vectores LENGTH "${vectores}" vectors)
    math(EXPR ultimo "${n_vectores} - 1")
    foreach(i RANGE ${ultimo})
        string(JSON id GET "${vectores}" vectors ${i} id)
        string(JSON veredicto GET "${vectores}" vectors ${i} expect verdict)
        string(JSON salida GET "${vectores}" vectors ${i} expect exit_code)
        string(JSON libreria ERROR_VARIABLE sin_libreria GET "${vectores}" vectors ${i} library)
        string(JSON archivo ERROR_VARIABLE sin_archivo GET "${vectores}" vectors ${i} file)
        if(veredicto STREQUAL "timeout")
            continue()
        endif()
        if(sin_libreria STREQUAL "NOTFOUND")
            set(objetivo "${PROBE_VECTORS_DIR}/${libreria}${LIBRARY_SUFFIX}")
        elseif(sin_archivo STREQUAL "NOTFOUND")
            set(objetivo "${PROBE_VECTORS_DIR}/${archivo}")
        else()
            continue()
        endif()
        sondear("vector ${id}" "${objetivo}" ${salida} json_vector)
        exigir_campo("vector ${id}" "${json_vector}" "verdict" "STRING" "${veredicto}")
        string(JSON motivo ERROR_VARIABLE sin_motivo GET "${vectores}" vectors ${i} expect reason)
        if(sin_motivo STREQUAL "NOTFOUND")
            exigir_campo("vector ${id}" "${json_vector}" "reason" "STRING" "${motivo}")
        endif()
        math(EXPR casos_esperados "${casos_esperados} + 1")
    endforeach()
else()
    message(STATUS "[probe_core] sin vectores del Engine (PROBE_VECTORS_DIR)")
endif()

if(NOT CASOS_CORRIDOS EQUAL casos_esperados)
    message(FATAL_ERROR
        "el oráculo corrió ${CASOS_CORRIDOS} casos; se esperaban ${casos_esperados}")
endif()
message(STATUS "[probe_core] ${CASOS_CORRIDOS} casos verificados")
