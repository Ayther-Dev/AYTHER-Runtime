# ---------------------------------------------------------------------------
# probe_core — el sondeo de cores del runtime (#589, PLAY-EP01.2).
#
# `--probe-core <dll>` existe para que Play pueda validar un core que el jugador
# eligió SIN cargarlo: cargar un core libretro es meter código GPL en el proceso
# del launcher, y esa es justo la frontera que el ecosistema no cruza. El
# runtime, que ya es GPL y es desechable, lo carga por él y se muere.
#
# Lo que este oráculo fija es el CONTRATO que Play consume, no la carga en sí:
#   · el código de salida distingue los tres desenlaces (0 / 2 / 3);
#   · la línea `AYTHER_STATUS` es un JSON de una sola línea con `event=probe`.
# Si alguien cambia un código o parte el JSON en dos líneas, Play no se rompe
# ruidosamente: muestra «no se pudo validar el core» para un core que anda, y
# el jugador queda sin manera de saber qué pasó.
#
# El caso bueno necesita la DLL del fork, que NO está versionada (ver
# `third_party/cores/core.lock`): en una máquina sin ella se saltea, y por eso
# los dos casos negativos —que no necesitan nada— son obligatorios. Un test que
# se saltea entero pasa en verde sin haber mirado nada.
# ---------------------------------------------------------------------------

if(NOT EXISTS "${RUNTIME_EXE}")
    message(FATAL_ERROR "no existe el binario del runtime: ${RUNTIME_EXE}")
endif()

set(CASOS_CORRIDOS 0)

# Corre el sondeo y verifica salida + código. `esperado_texto` vacío = no mira.
function(sondear etiqueta objetivo esperado_codigo esperado_texto)
    execute_process(
        COMMAND "${RUNTIME_EXE}" --probe-core "${objetivo}"
        OUTPUT_VARIABLE salida
        ERROR_VARIABLE  err
        RESULT_VARIABLE codigo
        TIMEOUT 60
    )
    if(NOT codigo EQUAL esperado_codigo)
        message(FATAL_ERROR
            "[${etiqueta}] el sondeo devolvió ${codigo}, se esperaba ${esperado_codigo}.\n"
            "Play decide con este número qué le dice al jugador.\n"
            "--- stdout ---\n${salida}\n--- stderr ---\n${err}")
    endif()
    # La línea del contrato: una sola, y con las comillas donde van.
    string(REGEX MATCH "AYTHER_STATUS \\{[^\n\r]*\\}" linea "${salida}")
    if(NOT linea)
        message(FATAL_ERROR
            "[${etiqueta}] no salió una línea AYTHER_STATUS completa (¿el JSON quedó partido en dos?).\n"
            "--- stdout ---\n${salida}")
    endif()
    if(NOT linea MATCHES "\"event\":\"probe\"")
        message(FATAL_ERROR "[${etiqueta}] el evento no es `probe`: ${linea}")
    endif()
    if(esperado_texto AND NOT linea MATCHES "${esperado_texto}")
        message(FATAL_ERROR "[${etiqueta}] falta `${esperado_texto}` en: ${linea}")
    endif()
    math(EXPR n "${CASOS_CORRIDOS} + 1")
    set(CASOS_CORRIDOS ${n} PARENT_SCOPE)
    message(STATUS "[probe_core] ${etiqueta}: ${linea}")
endfunction()

# 1. No está. Play lo ve al reabrir con un core que el jugador movió de carpeta.
sondear("archivo ausente" "${CMAKE_CURRENT_BINARY_DIR}/no-existe-este-core.dll"
        2 "\"reason\":\"no_carga\"")

# 2. Es una DLL de verdad, pero no es un core. El caso del jugador que apunta a
#    cualquier .dll del sistema; cargar sin mirar los símbolos lo dejaría pasar.
get_filename_component(BIN_DIR "${RUNTIME_EXE}" DIRECTORY)
set(NO_CORE "${BIN_DIR}/SDL3.dll")
if(EXISTS "${NO_CORE}")
    sondear("no es un core" "${NO_CORE}" 3 "\"reason\":\"no_es_libretro\"")
else()
    message(FATAL_ERROR "no se encontró SDL3.dll junto al runtime (${BIN_DIR}): "
                        "sin ella el caso negativo del sondeo no se prueba")
endif()

# 3. El core del fork, si esta máquina lo bajó. Es el único que comprueba que el
#    JSON bueno lleve los datos que Play muestra en la pantalla de core.
if(CORE_DLL AND EXISTS "${CORE_DLL}")
    sondear("core del fork" "${CORE_DLL}" 0 "\"ok\":true")
    # `api` y `library_name` son los campos que Play lee; que exista el JSON no
    # alcanza si vienen vacíos.
    execute_process(COMMAND "${RUNTIME_EXE}" --probe-core "${CORE_DLL}"
                    OUTPUT_VARIABLE salida TIMEOUT 60)
    foreach(campo "\"api\":1" "\"library_name\":\"[^\"]+\"" "\"valid_extensions\":\"[^\"]*md[^\"]*\"")
        if(NOT salida MATCHES "${campo}")
            message(FATAL_ERROR "el sondeo del core no informa ${campo}:\n${salida}")
        endif()
    endforeach()
else()
    message(STATUS "[probe_core] sin la DLL del fork (${CORE_DLL}): "
                   "el caso positivo se saltea — ver third_party/cores/core.lock")
endif()

if(CASOS_CORRIDOS LESS 2)
    message(FATAL_ERROR "el oráculo corrió ${CASOS_CORRIDOS} casos: no probó nada")
endif()
message(STATUS "[probe_core] ${CASOS_CORRIDOS} casos verificados")
