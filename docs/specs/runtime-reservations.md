# Compatibilidad de reservas con Play CE

Estado: alcance autorizado por el usuario el 2026-09-27: «Sí, publicar solo los cambios necesarios de compatibilidad». Se integra el contrato requerido por el Play CE local; no se modifican las decisiones de audio de Engine ni se desarrolla el comprobador de replay.

## Requisitos verificables

- RC-1: Cuando Play envíe la revisión de una ROM o pack, Runtime deberá reservar los archivos de la sesión y comprobar la revisión solicitada antes de confirmar la transferencia. Una revisión diferente o un archivo inaccesible deberá impedir la confirmación y el arranque del juego.
- RC-2: Mientras la sesión conserve la reserva en Windows, deberá impedir escritura, borrado y renombrado; deberá liberar los recursos al finalizar y también ante errores de preparación. Una plataforma que no soporte la reserva deberá rechazar una solicitud explícita sin simular éxito.
- RC-3: Cuando no se soliciten revisiones, Runtime deberá mantener el lanzamiento previo, incluidos orden de validación y diagnósticos. Las nuevas opciones y su confirmación son optativas; no cambian el protocolo de estado existente.
- RC-4: Cuando se publique el consumidor compatible, su versión, paquete Engine, hash del artefacto y contrato de lanzamiento deberán quedar documentados y validados con Play CE.

## Contrato y aceptación

Opciones: --rom-revision y --pack-revision. Formato compartido con Play: identidad del volumen/archivo/creación, tamaño, fecha de modificación en nanosegundos y USN, separados por los delimitadores existentes de Play. La confirmación técnica es AYTHER_RESERVATION 1 en stdout, únicamente después de obtener las reservas solicitadas y comprobar sus tokens. Los errores técnicos se emiten en inglés mediante el diagnóstico existente de preparación; Play conserva la presentación localizada.

Un rechazo usa el motivo técnico runtime.reservation_failed y salida 74 (error de E/S), sin atribuir a un pack un fallo de reserva de ROM. Se exige el test nativo de reserva/liberación y rechazo, una prueba real con tokens calculados por Play, y la suite de Runtime para proteger el lanzamiento sin las opciones nuevas. El token detecta cambios de revisión; no es un hash criptográfico del contenido ni acredita origen de un pack. No se incorporan cambios de caché, interfaz, autoría o audio a esta release.
