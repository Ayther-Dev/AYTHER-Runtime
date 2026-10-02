# Evidencia, recuperación y compatibilidad del comprobador de audio

`ayther_audio_qa` conserva evidencia técnica reabrible. Registra lo observado y
su integridad; no decide si existe un reinicio o una superposición musical.

## Replay visible

Añade `--presentation visible` a `ayther_audio_qa check` para abrir la presentación
normal de Runtime con el audio del dispositivo de reproducción. La imagen, la mezcla
y los registros proceden de una sola sesión alimentada con la toma. El modo
`--presentation none` (predeterminado) conserva la ejecución automatizada con audio
dummy; no acredita observación audiovisual.

El modo visible requiere la capacidad `visible_replay_v1`. Los controles de juego,
rewind y cambios de perfil están bloqueados durante la toma; el cierre de ventana
cancela y conserva lo disponible. La ventana se cierra después de drenar el audio
producido, sin seguir ejecutando el juego. Cada toma abre su propia ventana.

`runs/<run_id>/replay-result.toml` conserva atómicamente el terminal 1.3 con el
modo, backend de audio, perfil efectivo, cuadros presentados, conteo de cuadros
afectados y su intervalo envolvente (índices de toma desde cero). También conserva
resolución inicial, HD/shaders y la equivalencia física histórica como no verificada. El lector sigue
aceptando terminales 1.2 como presentación no solicitada. El intervalo envolvente
puede incluir cuadros no afectados; no representa una pérdida continua.

Un fallo de ventana, vídeo, audio o cadencia produce un resultado incompleto,
conservando el replay seguro y sus registros. El umbral de cadencia es un periodo
del core adicional al vencimiento del cuadro. Ningún estado completo implica que
se haya evaluado el fallo musical ni medido la sincronía física de los dispositivos.
Los campos `audible_restart_observed` y `audible_overlap_observed` muestran
`not_evaluated` cuando no existe evaluación, incluso si la ventana se presentó.
`false` queda reservado a una evaluación explícita negativa; no es el valor por defecto.

## Directorio de evidencia

La raíz de `--output` contiene `request-ledger.toml` y un directorio exclusivo
por `run_id`:

```text
<output>/
  request-ledger.toml
  runs/<run_id>/
    replay-result.toml
    fragments/facts-00000000000000000001.aqf
    audio/pcm-00000000000000000001.aqp
    checkpoints/current.toml
    query-index.toml
```

Algunos archivos aparecen únicamente al publicar su fase. Una ausencia se
informa como desconocida o incompleta y nunca se convierte en un conteo cero.
Una ejecución no reemplaza el directorio de otra.

## Hechos y audio

Cada `.aqf` comienza con `AYTFACT1`, versión 1.0 del contenedor, secuencia,
cantidad de registros, tamaño y SHA-256 del payload. El payload es un
`fact_batch`; sus registros admiten los esquemas 1.0–1.4 descritos en
`audio-qa-fact-format.md`. El formato vigente de hechos detallados es el binario
1.4 `AQF4`. La lectura valida encabezado, límites, longitud, secuencia, hash,
conteo y modelo antes de entregar hechos.

Cada `.aqp` comienza con `AYTPCM01`, versión 1.0 del contenedor, secuencia,
tamaños y SHA-256. El cuerpo conserva metadatos y PCM intercalado S16LE, S24LE,
S32LE o F32LE, hasta 192 kHz y ocho canales dentro de los límites negociados.
El lector valida captura, formato, timeline, rangos, hash y bytes. No inventa
silencio ni completa canciones; un WAV derivado deja intactos los `.aqp`.

## Durabilidad y recuperación

`checkpoints/current.toml` usa esquema 1.0 y secuencia creciente. Enumera hasta
4096 artefactos publicados con clase, ruta relativa segura, secuencia, tamaño y
SHA-256. El nuevo checkpoint se escribe y vacía antes del reemplazo atómico. Si
falla la publicación, el anterior permanece válido. El ledger aplica el mismo
principio y distingue solicitud nueva, reenvío, conflicto y sesión ocupada.

La auditoría reabre los artefactos en orden, verifica hechos, relaciones y
muestras, y conserva el primer punto inválido sin borrar el diagnóstico previo.
Una ejecución interrumpida se recupera como `incomplete`; el checkpoint queda
`absent`, `verified` o `unreadable`. `automatic_resume_allowed` siempre es
`false`: la recuperación no relanza la toma.

## Compatibilidad

Contenedores, PCM, checkpoints, ledger y referencias declaran sus versiones.
Los registros de hechos aceptan únicamente 1.0–1.4. Una versión no admitida,
truncamiento, exceso, hash distinto o ruta insegura devuelve un error explícito
sin migrar ni sobrescribir el original. Una futura migración deberá crear una
vista nueva.

Runtime negocia por separado los contratos Engine, Runtime QA, evidencia y
estado HD. Los cuatro deben ofrecer 1.0 y las diez capacidades obligatorias. Un
Runtime incompatible produce `runtime_incompatible` y no acredita replay.

## Aislamiento y materiales privados

Antes de iniciar, el comprobador fija las identidades de Runtime, ROM, core,
pack, toma, manifiesto y registro de confianza. Cada consulta y replay recibe
una raíz privada de datos de Runtime bajo la salida de la campaña. El
comprobador toma una instantánea completa del árbol ordinario de datos del
usuario, verifica que permanezca idéntico al terminar y elimina la raíz privada
por RAII. Una alteración produce `user_data_changed`; un aislamiento que no
puede prepararse produce `runtime_data_isolation_failed`.

La evidencia conserva identidades y procedencia, no bytes de ROM ni core. Pack,
tomas, manifiesto, registro de confianza y guardados ordinarios son entradas de
solo lectura. La campaña final verifica además que no queden raíces privadas y
que ningún archivo entregable coincida con los hashes de ROM o core.
