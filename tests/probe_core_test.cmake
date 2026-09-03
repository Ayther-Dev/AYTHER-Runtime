# ---------------------------------------------------------------------------
# probe_core — launcher-facing core metadata and failure contract.
#
# The positive fixture is built in this repository and exposes only the two
# Libretro symbols consumed by Engine rc.6's CoreProbe. The negative library is
# loadable but deliberately exports neither symbol. No downloaded core or
# external lock participates in this test.
# ---------------------------------------------------------------------------

foreach(required IN ITEMS RUNTIME_EXE CORE_DLL NON_CORE_DLL)
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
        COMMAND "${RUNTIME_EXE}" --probe-core "${objetivo}"
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

# 1. Missing file: launcher maps this to a load failure (exit 2).
set(archivo_ausente "${CORE_DLL}.missing")
if(EXISTS "${archivo_ausente}")
    message(FATAL_ERROR "el fixture de archivo ausente existe: ${archivo_ausente}")
endif()
sondear("archivo ausente" "${archivo_ausente}" 2 json_ausente)
exigir_campo("archivo ausente" "${json_ausente}" "ok" "BOOLEAN" "OFF")
exigir_campo("archivo ausente" "${json_ausente}" "reason" "STRING" "core.load_failed")

# 2. Loadable shared library without the required Libretro symbols (exit 3).
sondear("no es un core" "${NON_CORE_DLL}" 3 json_no_core)
exigir_campo("no es un core" "${json_no_core}" "ok" "BOOLEAN" "OFF")
exigir_campo("no es un core" "${json_no_core}" "reason" "STRING" "core.invalid")

# 3. Deterministic synthetic Libretro core (exit 0 and complete metadata).
sondear("core sintético" "${CORE_DLL}" 0 json_core)
exigir_campo("core sintético" "${json_core}" "ok" "BOOLEAN" "ON")
exigir_campo("core sintético" "${json_core}" "api" "NUMBER" "1")
exigir_campo("core sintético" "${json_core}" "library_name" "STRING" "AYTHER Synthetic Core")
exigir_campo("core sintético" "${json_core}" "library_version" "STRING" "1.0-test")
exigir_campo("core sintético" "${json_core}" "valid_extensions" "STRING" "aytest|rom")
exigir_campo("core sintético" "${json_core}" "need_fullpath" "BOOLEAN" "OFF")
exigir_campo("core sintético" "${json_core}" "block_extract" "BOOLEAN" "OFF")

if(NOT CASOS_CORRIDOS EQUAL 3)
    message(FATAL_ERROR "el oráculo corrió ${CASOS_CORRIDOS} casos; se esperaban 3")
endif()
message(STATUS "[probe_core] ${CASOS_CORRIDOS} casos verificados")
