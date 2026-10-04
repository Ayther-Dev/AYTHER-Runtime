# Evidencia, recuperación y compatibilidad del comprobador de audio

`ayther_audio_qa` conserva evidencia técnica reabrible. Registra lo observado y
su integridad; no decide si existe un reinicio o una superposición musical.

## Replay visible

Añade `--presentation visible` a `ayther_audio_qa check` para abrir la presentación
normal de Runtime con el audio del dispositivo de reproducción. La imagen, la mezcla
y los registros proceden de una sola sesión alimentada con la toma. El modo
`--presentation none` (predeterminado) conserva la ejecución automatizada con audio
dummy; no acredita observación audiovisual.

El modo visible requiere el protocolo QA 1.1 con las capacidades `visible_replay_v1` e
`inspection_v1`. Los controles de juego, rewind y cambios de perfil están bloqueados durante
la toma; ninguna tecla llega al juego. Cada toma abre su propia ventana, y cerrarla cancela
y conserva lo disponible.

### Controles de inspección

| Tecla | Efecto |
|---|---|
| Espacio | Pausa al terminar el frame en curso, que queda presentado; si no hay ninguno en curso, se queda en el último mostrado. En pausa, reanuda desde el frame siguiente al mostrado, sin acelerar para recuperar el tiempo de pausa. |
| ← / → | En pausa, retroceden o avanzan un frame dentro de la toma. La navegación es silenciosa y se recupera desde los checkpoints con las entradas grabadas. |
| I | Muestra u oculta el registro de depuración del frame. No cambia la reproducción ni la posición, y cada toma empieza con el registro oculto. |
| Re Pág, Av Pág, Inicio, Fin, rueda | Desplazan el registro de depuración visible. |

- Una tecla mantenida no se repite.
- Sin foco, ninguna tecla actúa. Al recuperarlo, una tecla que seguía pulsada queda bloqueada hasta que se suelta.
- ← y → a la vez no mueven nada.
- Durante la preparación o una recuperación, la ventana avisa «no disponible» u «ocupado» y no acumula órdenes.

**Fin natural.** Al completar el último frame de la última toma, el resultado lineal se confirma y se envía primero, y la ventana queda en pausa en ese frame. Una toma intermedia pasa a la siguiente, salvo que hubiera una pausa pendiente: entonces queda en pausa y Espacio inicia la siguiente.

**Inspección posterior.** Navegar después del fin natural abre un recorrido aparte, `post_end_inspection`, sobre una sesión rearmada desde los checkpoints. Ese recorrido tiene su propio `run_id` (`<run_id>-inspection-<n>`) y su propio terminal, y nunca cambia el resultado confirmado.

**Presentación interrumpida.** Una pérdida de vídeo, del dispositivo de audio en uso o de la ventana minimizada detiene la toma en el último frame completado. Al recuperarse, la toma queda en pausa con la incidencia registrada. La cancelación sigue disponible mientras está interrumpida.

`runs/<run_id>/replay-result.toml` conserva atómicamente el terminal con el
modo, backend de audio, perfil efectivo, cuadros presentados, conteo de cuadros
afectados y su intervalo envolvente (índices de toma desde cero). También conserva
resolución inicial, HD/shaders y la equivalencia física histórica como no verificada. El
terminal 1.4 añade, por separado, `playback` (`natural_end`, `cancelled`, `failed`,
`interrupted`), `traversal` (`linear`, `inspection`, `post_end_inspection`),
`linear_completed`, `evidence_reasons`, `ended_paused`, `user_pause_ms` e `interruptions`. El
lector sigue aceptando terminales 1.2 y 1.3, cuyo recorrido y reproducción se informan como
desconocidos, nunca como lineales. El intervalo envolvente
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
  requests/<run_id>/request-summary.toml
  runs/<run_id>/
    replay-result.toml
    traversal.toml
    fragments/facts-00000000000000000001.aqf
    audio/pcm-00000000000000000001.aqp
    checkpoints/current.toml
    query-index.toml
  runs/<run_id>-take-2/…
  runs/<run_id>-inspection-1/…
```

## Recorridos, visitas y resumen

- **`traversal.toml`** (esquema 1.1, uno por recorrido) describe cómo se recorrió la toma, y se reescribe de forma duradera al confirmar el resultado:
  - el tipo: `linear`, `inspection` o `post_end_inspection`;
  - los tramos lineales;
  - las visitas a frames, cada una con su número de visita;
  - las reanudaciones y las interrupciones.
- **Tipo de recorrido.** Una pausa sin navegación mantiene el recorrido lineal. El primer paso con ← o → lo convierte en `inspection` para siempre, y ese recorrido no se acredita como reproducción lineal completa.
- **`inspection_event`.** Cada control aplicado es un hecho de esta clase, con:
  - `seq`, el orden de visita desde 1;
  - el control: `pause`, `resume`, `step_forward`, `step_back`, `recover_failed`, `overlay_toggle`, `interrupted`, `recovered` o `advance_take`;
  - el frame antes y después, la visita y el tiempo de toma.
- **`render_frame`.** Cada visita a un frame, producido o recuperado, genera un hecho de esta clase con el frame, la visita, si se pudo componer o el motivo, el número de ocurrencias, y el tiempo de procesamiento y los FPS instantáneos cuando se midieron. Una visita repetida tiene otro número de visita. Un frame en pausa no genera registros nuevos.
- **Producción silenciosa.** Los hechos y el PCM producidos en modo silencioso, al navegar o recuperar, no entran en la evidencia: el Runtime los descarta en su sumidero.
- **`request-summary.toml`** (esquema 1.1) es el resumen confirmado y duradero de la solicitud:
  - las selecciones con su origen y su SHA-256, y `conditions_id`;
  - el resultado de cada toma por posición, con `playback`, `traversal` y `evidence` por separado;
  - el resultado conjunto.
  - Consultar una solicitud ya confirmada devuelve este resumen tal cual.
- **Evidencia completa.** Una evidencia es `complete` sólo si se conservaron y se comprobaron:
  - la identificación;
  - las condiciones;
  - todas las posiciones y controles;
  - el resultado;
  - los datos de cada frame visitado, sin pérdidas conocidas.

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

La validación previa calcula el SHA-256 de cada material (Runtime, ROM, core, pack, tomas,
manifiesto y registro de confianza) y lo guarda en el resumen de la solicitud. Durante cada
toma, el comprobador mantiene abiertos los materiales de esa toma con `FILE_SHARE_READ`, sin
permiso de escritura ni de borrado, y comprueba su SHA-256 contra el de la validación antes
de entregarlos al Runtime. Si algún material cambió, la toma no empieza y la solicitud se
detiene con `material_changed`, conservando la evidencia anterior. Cada consulta y replay recibe
una raíz privada de datos de Runtime bajo la salida de la campaña. El
comprobador toma una instantánea completa del árbol ordinario de datos del
usuario, verifica que permanezca idéntico al terminar y elimina la raíz privada
por RAII. Una alteración produce `user_data_changed`; un aislamiento que no
puede prepararse produce `runtime_data_isolation_failed`.

La evidencia conserva identidades y procedencia, no bytes de ROM ni core. Pack,
tomas, manifiesto, registro de confianza y guardados ordinarios son entradas de
solo lectura. La campaña final verifica además que no queden raíces privadas y
que ningún archivo entregable coincida con los hashes de ROM o core.
