# Dominio y contratos del comprobador de audio

Implementación incremental del comprobador. La CLI `ayther_audio_qa check` ejecuta replay mediante el Runtime ampliado y conserva evidencia reabrible; la evaluación musical continúa fuera de su alcance.

## Preparación, instalación y ejecución

El comprobador entregable y Runtime deben proceder del mismo build QA y del
lock `dependencies/ayther-engine.qa.lock.json`. Desde la raíz de Runtime:

```powershell
$env:VCPKG_ROOT = 'C:\Users\david\tools\vcpkg'
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine_qa.ps1
cmake --preset windows-ci -B out/build/windows-ci-audio-qa `
  -DAYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE=ON
cmake --build out/build/windows-ci-audio-qa --config RelWithDebInfo `
  --target ayther_runtime ayther_audio_qa
cmake --install out/build/windows-ci-audio-qa --config RelWithDebInfo `
  --prefix install/audio-qa
```

La instalación crea `bin/ayther_runtime.exe`, `bin/ayther_audio_qa.exe`, sus
DLL, shaders, fuentes, licencia y metadatos. Se debe elegir un prefijo nuevo;
el comando no necesita ni debe reemplazar una instalación usada como referencia.

Una solicitud real usa únicamente las opciones implementadas. La mínima indica
Runtime, ROM, al menos una toma y destino:

```powershell
$qa = 'C:\ruta\audio-qa\bin\ayther_audio_qa.exe'
$runtime = 'C:\ruta\audio-qa\bin\ayther_runtime.exe'
& $qa check `
  --runtime $runtime `
  --rom 'C:\ruta\juego.md' `
  --core 'C:\ruta\core.dll' `
  --pack 'C:\ruta\juego.ay' `
  --trust-registry 'C:\ruta\trust.toml' `
  --take 'C:\ruta\toma.ayr' `
  --output 'C:\ruta\evidencia' `
  --request-id 'solicitud-unica' `
  --language es
```

- `--rom` y `--take` son obligatorias. No hay toma por defecto: sin `--take` la
  solicitud se rechaza con `missing_required_option: --take` (RF-1.2).
- `--take` es repetible, conserva el orden y admite repeticiones explícitas: cada
  posición es una ejecución propia (`<run>`, `<run>-take-2`, ...) (RF-1.4).
- `--core` es opcional. Si falta, se toma del manifiesto de Play, de la referencia o,
  en último lugar, de la configuración de Play CE (`cores[plataforma]` y después
  `default_core` en `%APPDATA%\Ayther\play_config.toml`), que sólo se lee (RF-1.6).
- `--pack` es opcional; sin ella la solicitud se ejecuta «Sin pack» y el Runtime no
  recibe `--pack` (RF-1.3). `--pack-mode original` equivale a no cargar el pack.
- `--reference` y `--play-manifest` son opcionales y sólo aportan core y condiciones
  que no se hayan indicado. Si describen otra ROM, la solicitud se rechaza.
- Las condiciones se pueden indicar de forma explícita: `--profile`, `--subsystems`,
  `--mute-buses`, `--video-output`, `--patch`, `--shaders on|off` y `--core-option
  clave=valor` (repetible). La opción explícita precede al manifiesto y éste a la
  referencia (RF-1.5, RF-1.6).
- `--profile` sólo se aplica a un pack cargado. Sin pack (o con `--pack-mode original`)
  la solicitud se acepta con las mismas condiciones que la ejecución con pack: se muestra
  `profile=<pedido>` y `profile_effective=none source=generated`, y el Runtime no recibe
  `--profile`. Con pack, un perfil que el pack no ofrece se rechaza antes de admitir la
  solicitud con `profile_not_in_pack: --profile`; el sondeo del pack lista sus perfiles.
- Cada toma se valida entera antes de admitir la solicitud: además de cabecera, tamaños
  y entradas, su estado inicial se descomprime completo, como lo hará el Runtime antes de
  restaurarlo. Un estado dañado se rechaza con `take_initial_state_invalid: --take[i]`.
- `--request-id` es opcional; una repetición intencional debe usar otra identidad.
  `--trust-registry` sólo puede omitirse cuando la política efectiva permite abrir
  ese pack sin registro. La raíz `--output` mantiene el ledger y crea una ejecución
  exclusiva sin reemplazar las anteriores.

Antes de admitir la solicitud, cada valor efectivo se escribe con su origen:
`audio_qa_effective: <clave>=<valor> source=explicit|play_manifest|reference|environment|default|generated`.
El origen `explicit` significa que la opción se escribió, aunque su valor coincida con
el predeterminado (`--language es`); `default`, que no se escribió (RF-1.6).

`ayther_audio_qa options --format toml` publica el inventario de opciones de la
versión compilada, con el esquema de
`specs/002-AYTHER-bug-render/evidence/rf1-option-inventory-beta8.toml` (RF-1.8): `ref`
es la versión del Runtime y `commit`, el commit de las fuentes de la compilación (el
`HEAD` del checkout al compilar, o `AYTHER_RUNTIME_SOURCE_COMMIT` si se indica).

Los prefijos de diagnóstico tienen funciones distintas:

- `audio_qa_status` informa admisión y estados técnicos.
- `audio_qa_replay` identifica la toma, posiciones, relaciones y conservación.
- `audio_qa_summary` contiene el resultado agregado y su código.
- `audio_qa_error` conserva un código técnico estable.
- `audio_qa_message[es|en]` aporta el texto localizado; no se analiza como código.

Los códigos de salida son 0 para todas las tomas completas, 2 para resultado o
evidencia incompletos, 3 para invocación o solicitud inválida y 4 cuando no se
puede conservar evidencia mínima. La prioridad agregada es 4, 3, 2, 0. El
resultado no evalúa si el audio contiene un reinicio o una superposición.

La prueba sintética pública reproduce el recorrido completo sin material
privado. Genera su toma determinista, inicia Runtime real, conserva hechos y
PCM, reabre relaciones y espera código 0 con resultado técnico `complete`. Los
campos audibles permanecen en `false`: la prueba acredita ejecución e
integridad, no un veredicto musical. Runtime usa una raíz de datos privada y el
ensayo verifica que el árbol ordinario del usuario no cambie:

```powershell
cmake --build out/build/windows-ci-audio-qa --config RelWithDebInfo `
  --target ayther_runtime ayther_audio_qa `
           audio_qa_public_recording_fixture synthetic_libretro_core
ctest --test-dir out/build/windows-ci-audio-qa -C RelWithDebInfo `
  -R '^audio_qa_real_replay_integration$' --output-on-failure
```

La consulta de una relación ya conservada usa `query`:

```powershell
& $qa query --index 'C:\ruta\run\query-index.toml' `
  --run-id '<RUN_ID>' --producer-id '<PRODUCTOR>' --sequence 1 --language es
```

Los formatos `.aqf` y `.aqp`, checkpoints, recuperación parcial, rechazo de
versiones y preservación de originales se describen en
[`docs/audio-qa-evidence.md`](../../docs/audio-qa-evidence.md).

QA-090 añade el decodificador de la cabecera QA 1.0: consume solo los 40 bytes
fijos, interpreta enteros little-endian y valida magic, versión exacta, tamaño,
tipo, flags, longitud, dirección/canal, secuencia y reserva. El resultado solo
expone metadatos después de validar; no lee ni reserva el payload. Admite de 1 a
262104 bytes de cuerpo, para un mensaje total máximo de 256 KiB.

QA-091 añade el primer mensaje de control completo: Request pendiente viaja en
TOML sobre tipo request/canal 1 y la respuesta aceptada o rechazada vuelve como
session_status/canal 2. Ambas rutas validan cabecera, secuencia, tamaño exacto,
tipo, admisión y esquema antes de entregar el modelo; todos los campos de
identidad y la lista ordenada de tomas sobreviven el recorrido.

QA-092 añade `fact_batch`: entre 1 y 1024 registros, cada uno precedido por
longitud u32 LE y limitado a 64 KiB incluido el prefijo. El lector conserva los
esquemas TOML 1.0–1.3 y el binario detallado 1.4 `AQF4`; el escritor actual usa
1.1 para hechos compactos y 1.4 para hechos con campos. Se conservan productor,
secuencia, frame, causas, decisiones, asignación, ocurrencia, motivo, órdenes y
campos tipados. Conteos, prefijos, versión y bytes residuales se validan antes
de entregar el lote.

QA-093 añade `audio_chunk`: dos longitudes u32 LE delimitan metadatos TOML y
PCM intercalado dentro del mensaje de datos. El codec conserva S16LE, S24LE,
S32LE y F32LE, tasa, canales, rango semiabierto, bytes, discontinuidades,
causas, durabilidad y checkpoint. Exige y verifica SHA-256 de los bytes PCM,
comprueba el producto sample frames × canales × ancho y rechaza mensajes que,
sumados metadatos y prefijos, exceden 256 KiB.

QA-094 añade el adaptador de canal de datos heredado. La pareja RAII deja el
extremo lector del supervisor sin herencia y el extremo escritor destinado a
Runtime heredable, valida el token recibido y ofrece lectura/escritura completa
sin usar stdout ni stderr. En Windows, el supervisor debe crear al hijo con una
lista explícita de handles; la integración prueba que un pipe heredable omitido
no cruza el límite de proceso y que el cierre produce EOF y libera los handles.

QA-095 añade el puente de observación Runtime sobre la API pública instalada de
Engine. Reserva una cola SPSC fija por productor para hechos y PCM, copia todas
las vistas prestadas dentro del callback y las entrega después al consumidor
con el `run_id` inmutable de la sesión. Saturación, entradas inválidas y
emisiones después del cierre se contabilizan fuera de las colas; el callback no
serializa, espera ni realiza E/S.

QA-096 añade la validación fija de la cabecera de toma por contenido. Reconoce
`ARP1`, admite las versiones 2 a 8 de Engine y devuelve códigos distintos para
entrada vacía, cabecera truncada, magic inválido y versión no admitida. El
lector no recibe el nombre de archivo, por lo que `.ayr` y `.arp` no influyen
en la aceptación. Tamaños variables y descompresión continúan en QA-097.

QA-097 valida el layout variable antes de reservar o descomprimir. Aplica B02:
toma y estado inicial descomprimido de hasta 64 MiB y entre 1 y 54.000 cuadros.
Con aritmética de rangos comprueba strings, estado comprimido e inputs contra el
tamaño total, conserva sus offsets y rechaza exceso o truncamiento sin crear
buffers a partir de longitudes externas.

QA-098 adapta el rango validado de inputs a una fuente secuencial por cuadro.
Entrega el bitmask RetroPad u16 little-endian desde el cuadro 0 hasta N−1 y
finaliza sin repetir. No recibe ni consulta trim, estadísticas o historias
autoradas, por lo que siempre consume la duración completa declarada.

QA-099 descomprime el estado inicial acotado, valida la fuente antes de mutar
Engine y conserva el resultado completo de la restauración. Solo después del
éxito expone los inputs. El adaptador instalado llama directamente
`AytherSession::unserialize`; el ensayo fija el orden restauración, entrada 0 y
primer step.

QA-100 fija la política ante fallo: un replay QA no consulta entrada ordinaria
ni grabada y no ejecuta el cuadro cero; la partida ordinaria conserva el aviso
y el arranque desde cero con su input habitual. Los códigos distinguen fallo de
preparación, restauración no intentada y rechazo de Engine.

QA-101 selecciona el origen HD inicial mediante una política de dominio y un
adaptador del paquete Engine instalado. La ausencia usa `fresh` verificado sin
volver incompleta la ejecución. Un estado aportado exige identidad, cuadro y
los tres payloads completos; un rechazo conserva código y sección y ejecuta
`fresh` para eliminar cualquier aplicación parcial.

QA-102 añade una barrera propietaria de publicación. Valida que el juego esté
restaurado y la descripción HD completa, entrega la instantánea una sola vez y
solo después de la confirmación permite leer la entrada grabada siguiente. Un
rechazo, estado incompleto o consumidor ausente mantiene intacto el cursor.

QA-103 conecta esa fuente con las APIs públicas habituales de Engine. Cada
bitmask grabado pasa una vez por `AytherSession::set_input(0, buttons)` y cada
entrada confirmada produce un solo `AytherSession::step()`. El bucle no usa
`replay_seek`, no ejecuta tras N y queda terminal si una operación falla.

QA-104 incorpora una compuerta de controles externos alrededor del replay.
Entrada de juego por teclado, rewind y fast-forward se descartan sin tocar el
cursor grabado ni ejecutar cuadros. La cancelación permanece disponible como
petición separada y consultable; su aplicación en un límite seguro pertenece a
una tarea posterior.

QA-105 fija perfil, máscara de mute y velocidad racional en un valor propietario
durante la ejecución. Cambios de configuración y recargas externas se comparan
con esa instantánea, informan los campos discrepantes y se rechazan sin mutarla;
una recarga idéntica se reconoce como no-op.

QA-106 conecta un publicador de progreso al bucle. La aceptación no se presenta
como inicio efectivo; cada entrada consumida precede a su cuadro completado y el
fin solo se emite tras comprobar N entradas y N cuadros, sin paso adicional.

QA-107 conecta ese fin con las APIs públicas de cierre de Engine. Congela la
producción, finaliza voces y drena solo el staging previo al límite, conservando
los tres resultados y sin ofrecer una operación que pueda ejecutar otro cuadro.

QA-108 añade la máquina pura de fases `preparing`, `ready`, `playing`, `closing`
y `closed`. Rechaza saltos y resultados no terminales sin depender de procesos,
SDL, serialización o disco.

QA-109 separa el terminal de reproducción del resultado de conservación. Un
fallo posterior puede dejar evidencia incompleta sin reescribir `natural_end`;
un estado de evidencia pendiente no puede publicarse como ejecución cerrada.

QA-110 reserva una sesión desde preparación hasta cierre completo. Otra
solicitud independiente recibe `busy` con la identidad activa y nunca se encola
ni interrumpe la ejecución; solo su fase `closed` libera la reserva.

QA-111 registra solicitudes por identidad y contenido. Un reenvío idéntico
devuelve el estado conocido sin duplicar la ejecución; reutilizar el ID con otra
sesión, condiciones o lista de tomas se rechaza como conflicto.

QA-112 distingue una repetición intencional mediante una identidad de solicitud
nueva. La referencia puede coincidir, pero cada identidad conserva su propia
ejecución y evidencia; admitir la repetición no reemplaza el resultado anterior.

QA-113 convierte las tomas seleccionadas en ejecuciones ordenadas e
independientes. Una toma no seleccionada no se programa y el resultado de una
ejecución no puede sustituir el de otra.

QA-114 captura la petición de cancelación y la aplica en un límite de cuadro.
Conserva posiciones de petición y aplicación, último cuadro ejecutado y el
terminal `cancelled`, sin consumir la siguiente entrada grabada.

QA-115 arbitra los terminales de reproducción una sola vez. El primer terminal
efectivo conserva resultado y coordenadas; las notificaciones tardías se
reconocen sin reescribirlo.

QA-116 resuelve el resultado de conservación por separado. Un fallo de drenaje
o escritura cierra la evidencia como incompleta y conserva el terminal de juego
y el último límite durable conocido.

QA-117 modela la desconexión y el vencimiento sin confirmación como estados
distintos de una salida observada. Conserva el último `Run` conocido y no lo
presenta como cerrado hasta recibir evidencia explícita del cese.

QA-118 identifica el ejecutable de Runtime y lo revalida antes de lanzarlo con
la API de proceso Windows. Los argumentos se codifican individualmente, el
entorno se crea desde una lista permitida y no se utiliza un intérprete de shell.

QA-119 mantiene el inicio de juego detrás de la negociación de contratos,
capacidades y límites. Un requisito ausente o incompatible deja diagnóstico y
cero inicios; una oferta completa autoriza una sola transición.

QA-120 drena hechos, PCM y logs con lectores y límites independientes. La
presión en `stderr` no detiene los datos QA y ningún canal mezcla sus bytes con
otro.

QA-121 entrega la cancelación por un canal de control heredado y la representa
como petición, acuse, aplicación con posición y cese confirmado. Cada estado es
versionado y el cese solo se registra después de observar la salida del hijo.

QA-122 vigila la preparación con límites de 120 segundos sin progreso y 300
segundos totales. Solo una etapa nueva acredita avance; un heartbeat o repetir
la misma etapa no amplían el plazo. El diagnóstico conserva la última etapa y
declara la evidencia incompleta.

QA-123 vigila la reproducción con un límite de 5 segundos sin cuadro
completado. Heartbeats, repeticiones y retrocesos no acreditan avance; solo un
cuadro mayor reinicia el plazo y queda conservado en el diagnóstico.

QA-124 vigila el cierre con 10 segundos sin progreso durable y 30 segundos
totales. Recibir bytes actualiza el diagnóstico, pero solo confirmar más bytes
duraderos reinicia el plazo. Ambos contadores se conservan por separado.

QA-125 acota la cancelación tras bloqueo o pérdida de canal. Conserva si la
petición pudo entregarse, exige respuesta en 2 segundos y termina la espera a
los 5 segundos desde la misma petición, sin confundir vencimiento con cese.

QA-126 mantiene la sesión ocupada hasta observar `closed` y cese confirmado
para la misma ejecución. Un cierre sin confirmación y un estado de proceso
ajeno no liberan la reserva ni permiten iniciar otra toma.

QA-127 conserva el orden de las tomas y exige una identidad de proceso nueva
para cada una. Un fallo aislado permite continuar solo después de confirmar su
cese; reutilizar el proceso anterior no consume la toma pendiente.

QA-128 lee el `launch.toml` de Play CE como referencia inicial. Materiales y
condiciones conservan la procedencia del manifiesto; ceros y cadenas vacías
escritos siguen siendo explícitos, mientras que una clave ausente no recibe un
valor predeterminado.

QA-129 contrasta la selección resuelta del lock con el ejecutable configurado
y medido. Solo una coincidencia de ruta y contenido hereda release y commit;
una ruta diferente crea una referencia ampliada, conserva su procedencia y no
se presenta como la versión fijada aunque los bytes coincidan.

QA-130 fija la toma en una imagen propietaria de hasta 64 MiB e identifica
exactamente esos bytes por SHA-256 y tamaño. El replay no vuelve a abrir la
ruta; una sustitución posterior no cambia la imagen y una identidad esperada
distinta rechaza la preparación.

QA-131 fija el pack y sus recursos externos registrados en un directorio
privado nuevo. La copia calcula SHA-256 y tamaño mientras consume los bytes,
rechaza crecimiento, sustitución o identidad distinta y aplica 8 GiB al
conjunto. La resolución posterior solo admite rutas lógicas registradas; un
staging preexistente se conserva y la limpieza solo alcanza el creado por la
operación.

QA-132 fija ROM y core en rutas privadas estables que conservan la extensión
necesaria para cargarlos. La ejecución recibe esas rutas verificadas; la vista
de evidencia solo contiene rol, SHA-256 y tamaño con contenido excluido, y no
expone rutas ni bytes privados.

QA-133 crea un directorio de guardados privado y disjunto por sesión y lo
selecciona mediante la precedencia existente de RuntimeConfig. Antes y después
conserva una instantánea acotada de estructura, tamaños y SHA-256 del árbol del
usuario; no sigue enlaces y detecta cualquier mutación sin escribir en ese
árbol.

QA-134 construye RuntimePaths sobre una raíz QA privada y disjunta. Preferencias
por juego y pack, guardados predeterminados, capturas y diagnóstico quedan bajo
esa raíz; la sesión normal conserva su directorio y sus bytes, verificados con
la misma instantánea acotada.

QA-135 añade una puerta de consumo para materiales que no pudieron fijarse. La
identidad medida se conserva en una observación separada: solo una coincidencia
con la referencia inicial entrega permiso. Un cambio o una identidad no
verificable bloquean el consumo, marcan la evidencia incompleta y registran el
material, la etapa, el rango de muestras y el último cuadro disponibles, sin
reescribir la referencia inicial con los bytes encontrados.

QA-136 inicia el almacén de evidencia con una reserva exclusiva por `run_id`.
La creación de la ruta de ejecución es indivisible; una colisión conserva el
contenido anterior y produce `evidence_directory_collision` con resultado
incompleto. Cada ejecución distinta recibe otra ruta y los identificadores con
separadores o recorrido ascendente se rechazan antes de tocar el filesystem.

QA-137 escribe `reference.toml` 1.0 mediante creación exclusiva del sistema
operativo. El documento conserva la referencia completa, hashes y tamaños,
procedencia por campo y ausencias atribuidas. La reapertura calcula la identidad
del documento y reproduce el valor original; otra iteración usa otro directorio
y no puede truncar ni reemplazar la referencia anterior. El vaciado durable y
los checkpoints se incorporan en sus tareas posteriores.

QA-138 escribe lotes inmutables en `fragments/facts-<sequence>.aqf`. El sobre
`AYTFACT1` 1.0 conserva secuencia, cantidad, tamaño y SHA-256 del mensaje
`fact_batch`; el lector valida esos metadatos, el hash, la secuencia interna y
los hechos antes de entregarlos. Una ruta existente nunca se reemplaza. El
vaciado durable y la publicación mediante checkpoint se incorporan en
QA-140–QA-142.

QA-139 escribe cada bloque en `audio/pcm-<sequence>.aqp`. El sobre `AYTPCM01`
1.0 autentica el mensaje `audio_chunk`, que conserva sus propios metadatos,
rango, discontinuidades, bytes y SHA-256 PCM. La reapertura verifica ambos
niveles sin depender de una cabecera WAV final; una ruta existente no se
reemplaza. La fuerza durable empieza en QA-140.

QA-140 publica bytes durables desde un temporal exclusivo situado en el mismo
directorio. En Windows completa la escritura, ejecuta `FlushFileBuffers` y
publica con `MoveFileExW` y `MOVEFILE_WRITE_THROUGH`; solo entonces devuelve
éxito. Un fallo de reemplazo conserva el destino anterior y elimina el
temporal. Los checkpoints empiezan en QA-141.

QA-141 publica `checkpoints/current.toml` 1.0 con secuencia creciente. Solo
acepta comprobantes emitidos por QA-140 y vuelve a validar tipo, secuencia,
ruta relativa e identidad de cada fragmento antes de listarlo. La sustitución
es durable y atómica; una actualización fallida conserva la versión anterior.

QA-142 mantiene contadores separados de bytes recibidos y duraderos. Solicita
una única publicación al primer umbral de 1 segundo o 1 MiB pendiente; alcanzar
otro umbral durante esa escritura informa saturación. La solicitud no adelanta
el límite durable: solo lo hace la confirmación de un `StoredCheckpoint`
creciente emitido por QA-141. Los relojes y contadores que retroceden se
rechazan sin cambiar el estado.

QA-143 publica `request-ledger.toml` 1.0 en la raíz de salida. Conserva hasta
1024 solicitudes con sus documentos Request/Run 1.0 y una generación creciente,
dentro de un límite de 64 MiB. Cada alta o actualización se publica mediante
la operación durable de QA-140 antes de mutar la vista en memoria. Al reabrir,
un `request_id` idéntico devuelve `known` con su estado vigente y no autoriza
otro lanzamiento; reutilizarlo con contenido distinto devuelve conflicto.

QA-144 recupera un Run que no alcanzó `closed` sin reanudarlo. Reabre y valida
el checkpoint vigente, conserva fase, terminal de reproducción, posiciones y
estado de cese, y publica únicamente `evidence_result=incomplete` en el ledger.
La vista distingue checkpoint verificado, ausente o ilegible y siempre expone
`automatic_resume_allowed=false`. Repetir la recuperación es idempotente.

QA-145 permite auditar un checkpoint cuyo artefacto ya no es verificable. Los
artefactos se comprueban en el orden registrado; la vista conserva el prefijo
íntegro e identifica el primer descriptor que falla junto con
`artifact_invalid` o `artifact_changed`. La auditoría es de solo lectura: no
borra, repara ni sustituye fragmentos truncados o corruptos.

QA-146 fija lectura estricta 1.0 para ledger y checkpoint. Un major o minor
desconocido se rechaza como incompatible y cualquier campo no declarado en el
documento o sus artefactos se rechaza como malformado. La validación nunca
reescribe ni migra el original rechazado.

QA-147 deriva una vista RIFF/WAVE desde hasta 4096 bloques autenticados y 64 MiB
de PCM. Todos deben pertenecer a la misma ejecución, punto de captura, formato y
timeline, con rangos exactamente contiguos. Los bytes se concatenan sin
conversión y la vista se publica durablemente. Un hueco devuelve
`non_contiguous`; no se rellena con silencio ni se crea un WAV parcial.

QA-148 añade un presupuesto opcional de bytes escribibles a la publicación
durable. Al agotarlo después de un prefijo devuelve `storage_exhausted`, cierra
y elimina el temporal y conserva el destino anterior. Los consumidores no
reciben comprobante durable y el checkpoint informa `publish_failed`.

QA-149 añade puntos de interrupción deterministas antes del vaciado y después
del vaciado pero antes de la publicación. Ambos resultados siguen siendo datos
pendientes: eliminan el temporal, preservan el destino confirmado y no emiten
`DurablePublishedFile`. Solo la sustitución atómica confirma durabilidad. En
Windows, una reserva que niega el borrado fuerza también el fallo de reemplazo
sin modificar la última versión confirmada.

QA-150 reabre los fragmentos autenticados y audita la continuidad de sus
secuencias y de las secuencias propias de cada productor. Una retransmisión con
el mismo `fact_id` y contenido idéntico se contabiliza como idempotente. Un
salto o la reutilización del identificador con contenido distinto conserva el
primer punto afectado y marca el tramo como `incomplete`.

QA-151 difiere la resolución de `cause_ids` internos hasta haber reabierto el
tramo completo. Un antecedente que llega en un fragmento posterior se resuelve;
un `PreexistingContext` no se busca dentro de la ejecución. Si al cierre sigue
faltando un antecedente interno, se conservan los dos identificadores del
vínculo y el tramo queda `incomplete`.

QA-152 mantiene separado el orden de llegada del orden efectivo. Los hechos
con `shared_state_order=not_applicable` permanecen independientes. Para cada
estado compartido con orden conocido, la auditoría reúne sus secuencias por
ejecución y exige continuidad; un hueco identifica `state_id`, valor esperado
y valor observado y deja la traza `incomplete`.

QA-153 reabre bloques PCM autenticados y valida secuencia, identidad de captura,
formato, timeline y continuidad de sample frames. Un hueco, solapamiento o
retroceso conserva el intervalo afectado. Cada límite de efecto conocido debe
estar contenido en su bloque; un límite desconocido o un silencio insertado sin
rango de salida verificable deja el tramo `incomplete` sin generar audio.

QA-154 resume inventario y carga con una fuente de expectativa explícita. La
referencia inicial puede fijar 14, mientras otro pack deriva su esperado solo
de un inventario completo propio. Las retransmisiones idénticas por
`pack_content_id + ordinal` no aumentan cantidades; ordinales diferentes no se
fusionan aunque compartan identidad autorada. Un duplicado conflictivo conserva
la identidad afectada y no publica recuentos de carga inequívocos. El resumen
mantiene separado inventario observado, esperado y estados desconocido,
pendiente, cargado y rechazado, con un máximo de 4096 observaciones por llamada.

QA-155 mantiene candidatura, selección, inicio, avance y final como etapas de
cobertura independientes. Cada una conserva extensión, cantidad y hecho de la
misma ejecución. Una cantidad positiva es observada; un cero completo es una
ausencia observada y un cero parcial permanece desconocido. El resumen no
infiere inicio desde selección, avance desde inicio ni final desde avance, y
rechaza la mezcla de hechos pertenecientes a otra ejecución.

QA-156 produce un informe de pendientes desde el inventario y las etapas
observadas. Mantiene un contador de positivos aun con fuente parcial, pero solo
publica un total de detecciones conocido cuando la fuente está completa. Una
fuente ausente deja ese total desconocido y no lo convierte en cero. La lista de
pendientes conserva identidades de pack y ordinal. La cobertura parcial se
informa aparte y no modifica el resultado de integridad técnica recibido.

QA-157 agrega cobertura de varias tomas por `pack_content_id + ordinal`. Una
asignación repetida ocupa una sola entrada, mientras cada observación de etapa
mantiene su `run_id` y sus datos originales. Esto conserva diferencias como
inicio cero en una toma e inicio positivo en otra. Una segunda fila para la
misma ejecución y asignación se rechaza para evitar duplicar cobertura.

QA-158 consulta el recorrido de un evento hasta un tramo de audio. Las rutas
explícitas se forman solo con `cause_ids` y devuelven todos los identificadores
y el rango de salida conocido. Una correlación temporal requiere un enlace
aportado con motivo y se etiqueta como tal; no se presenta como causalidad. La
consulta está acotada a 4096 hechos, tramos y enlaces, valida referencias y no
inventa un rango cuando el tramo relacionado no lo expone.

QA-159 consulta coexistencia contra una marca de transición aportada. Intersecta
solo intervalos con timeline y tasa iguales y conserva la procedencia de la
marca y de cada ocurrencia. Las causas de inicio siguen siendo hechos,
`PreexistingContext`, una combinación o desconocidas; el contexto previo nunca
se transforma en un disparo. Más de una voz solapada acredita coexistencia.

QA-039 añade CapabilitySet: versiones ofrecidas de los cuatro contratos, diez capacidades obligatorias de plan §4.1 y límites negociados. Solo acepta 1.0 explícitamente ofrecida; ausencia, duplicados, listas excesivas o capacidad insuficiente generan diagnóstico. Máximos: 64 capacidades y 16 versiones por contrato. No anuncia que el binario actual implemente esas capacidades.

QA-038 añade Assignment/Coverage/Diagnostic: entradas por pack+ordinal, identidad autorada separada, etapas observadas independientes y diagnóstico técnico. Candidatura/carga/contexto previo no equivalen a selección; cero parcial o no observado no equivale a ausencia completa. No contiene veredicto musical. Detalles de diagnóstico: hasta 4096 bytes.

QA-037 añade InitialState y versión HD 1.0: detector, ventanas, voces/posiciones, solicitudes y audio pendiente, con ausencia verificada diferenciada de desconocimiento. Las imágenes de continuación son propiedad de Engine. `has_complete_hd_description` valida que la descripción requerida está presente; QA-101 conecta la restauración pública y QA-102 ordena su publicación. Exportación durable y verificación de hashes siguen pendientes. Topes: 256 voces/colas, 4096 ventanas/solicitudes, 64 MiB por imagen y límite agregado B02 al importar.

QA-033 añade reference_model: referencias inicial/ampliada, builds, condiciones y materiales con procedencia por campo, separando identidad identificada y consumida. well_formed comprueba consistencia estructural, no verifica archivos ni autentica evidencias. Límites: 4096 materiales, 256 condiciones, 256 diferencias y 4096 bytes por valor; las identidades mantienen 256 bytes. Referencias incompletas conservan sus ausencias y pueden servir de diagnóstico sin acreditarse equivalentes.

QA-034 añade fact_model: identidad por ejecución/productor/secuencia, hasta 256 antecedentes y 256 órdenes de estado compartido por hecho, y disponibilidad explícita. Permite varios antecedentes, contexto previo y referencias futuras, sin inventar orden por llegada. La resolución final del grafo y emisión de productores son tareas posteriores; los límites codificados B06 siguen aplicándose además de los del modelo.

QA-035 añade mix_model: identidades de ocurrencia separadas de clave, posiciones de pista y rangos semiabiertos en sample_frames con tasa y timeline. MixSpan admite 256 participantes, conserva original/gain/mute y diferencia rango de mezcla de salida. overlaps solo compara timelines y tasas iguales; la ausencia de correspondencia produce resultado desconocido. No es un detector de fallos musicales.

QA-036 añade audio_chunk: PCM intercalado S16LE/S24LE/S32LE/F32LE con tasa, canales, rango de sample_frames, hash y checkpoint disponibles. Valida tamaño sin overflow y separa conocido/desconocido/no aplicable. Admite hasta 192 kHz, ocho canales, 256 KiB de payload y 256 discontinuidades; el framing aplica además su límite codificado. Validar el modelo no autentica un hash ni confirma persistencia física.

model.h contiene valores propios del dominio sin Engine, SDL, TOML, procesos ni disco. Request identifica sesión, condiciones inmutables por identidad y tomas ordenadas; las repeticiones de una toma en la lista se conservan. Run separa phase, playback_result, evidence_result y equivalence_result. Los contadores ausentes son desconocidos; cero es un valor conocido. last_durable_sample es una frontera exclusiva en sample_frames.

model_toml es un adaptador de memoria, basado en tomlplusplus 3.4.0 ya fijado por Runtime. No escribe ni confirma durabilidad. Su esquema 1.0 conserva los uint64 como cadenas decimales, sin conversión por float. Rechaza versiones no admitidas, tipos/campos/enum inválidos y metadatos superiores a 256 KiB antes del parser. Las identidades tienen 1–256 bytes sin controles y una solicitud admite 1–1024 tomas, sujeta además al máximo serializado. No se trunca ninguna entrada. Las transiciones y la idempotencia se implementarán en sus tareas posteriores; el codec no deduce resultados ni declara equivalencia.

Desde Runtime, ensayo independiente con las dependencias instaladas:

```powershell
cmake -S tests/audio_qa -B out/build/qa-domain -G 'Visual Studio 18 2026' -A x64 -T v145 `
  '-Dtomlplusplus_DIR=C:/Users/david/Workspaces/ayther/AYTHER-Runtime/out/build/rc9/vcpkg_installed/x64-windows/share/tomlplusplus'
cmake --build out/build/qa-domain --config Release --parallel 4
ctest --test-dir out/build/qa-domain -C Release --output-on-failure
```

También se registra en el CTest normal de Runtime. Los targets nuevos activan clang-tidy con el preset de calidad; las cabeceras del dominio están incluidas en su filtro y en la revisión de supresiones. La prueba usa la DLL de tomlplusplus correspondiente a la configuración Debug o Release del paquete existente.

RF-3, RF-11, RF-12 y RF-13: pruebas de orden de tomas, cierre con reproducción natural y conservación incompleta, ausencia frente a cero, uint64 máximo, versiones/tipos inválidos y fronteras de tamaño. La evidencia de tareas y los contratos se mantienen en specs/001-AYTHER-bug-audio-qa del workspace.

QA-170 conecta la entrada `check` con un proceso Runtime real mediante dos
canales heredados explícitos. Runtime valida la solicitud AYQA 1.0, restaura la
toma en `AytherSession`, prepara audio HD nuevo y ejecuta cada entrada grabada
una vez por `set_input` y `step`. El terminal devuelve cantidad declarada,
cantidad consumida e identidades SHA-256 de los estados inicial y final. La
opción `--trust-registry` permite que el mismo Runtime valide el pack firmado;
no se transmite una segunda sesión Engine ni se acepta un doble de Runtime.

La integración pública se ejecuta con:

```powershell
ctest --preset windows-ci -R "^audio_qa_(public_recording_fixture|real_replay_integration)$" --output-on-failure
```

QA-171 activa el audio lógico de la sesión QA, conecta el observador de Engine
al puente acotado de Runtime y drena sus hechos fuera de los productores. El
verificador no usa orden de llegada: recorre `cause_ids` desde
`detector_input_batch` hasta candidato, selección y solicitud, y exige las dos
ramas de esa solicitud hacia decisión/efecto y `hd_mix_participant`. Solicitud,
decisión, efecto y mezcla deben compartir la misma ocurrencia. El terminal AYQA
1.1 conserva las identidades de esas siete etapas, la ocurrencia, el total de
hechos y el estado de pérdidas. Una pérdida, identidad ajena o relación ausente
produce `audio_trace_incomplete`.

QA-172 transporta los hechos y bloques PCM reales antes del terminal. La CLI
los publica bajo un directorio exclusivo con los formatos versionados `.aqf` y
`.aqp`, abre de nuevo cada archivo y compara hechos, metadatos, hashes y bytes.
Las relaciones de QA-171 se recorren otra vez sobre los hechos reabiertos. Solo
entonces se informa `relationships_reopened=true`.

La ejecución sintética conserva además un diagnóstico real: el orden compartido
`audio_detector_live` contiene huecos aunque el puente no perdió hechos. Por
eso se informa `fact_integrity_complete=false` y el resultado general continúa
`incomplete`; la conservación no convierte el orden ausente en uno observado.

QA-173 conecta la negociación de capacidades con la CLI real. Antes de crear
canales o pasar core, ROM y pack, el comprobador ejecuta únicamente
`--qa-capabilities`, captura hasta 64 KiB durante un máximo de 2 segundos y
valida los cuatro contratos 1.0, las diez capacidades y sus límites. Un Runtime
antiguo o una oferta inválida produce `runtime_incompatible` con resultado
incompleto; no se abre una sesión de juego ni se acredita replay. La prueba
opcional `audio_qa_legacy_runtime_cli` recibe por CMake la ruta de un binario
histórico real y verifica que no aparecen fragmentos `.aqf` o `.aqp`.

QA-186 aplica la cuota B04 antes de aceptar bytes de evidencia. Una ejecución
requiere 10 GiB libres al admitirse y puede contabilizar hasta 8 GiB entre
artefactos duraderos, temporales y escrituras en vuelo. Dentro de ese total se
reservan 1 MiB para diagnóstico y checkpoint terminales. El exceso o la falta
de espacio produce un diagnóstico de almacenamiento con resultado
`incomplete` y conserva la secuencia del último checkpoint; no se contabilizan
como aceptados los bytes rechazados.

QA-187 aplica B03 al inventario privado seleccionado por una solicitud. El
conjunto no puede superar 8 GiB y la comprobación exige, en cada ejecución, los
bytes todavía por fijar más 2 GiB de margen y 10 GiB reservados para evidencia.
Los materiales ya fijados cuentan para el máximo, pero no se reservan otra vez.
Las entradas no seleccionadas no participan del presupuesto y no se entregan a
las operaciones de staging.

QA-188 mide B05 en procesos hijos aislados. Cada perfil compara captura
apagada y encendida en tres pares, muestrea memoria privada cada 10 ms y valida
el máximo de cada par sin promediar. El perfil de captura instancia el bridge
real de producción con nueve productores, 128 hechos y ocho bloques PCM por
productor; su tamaño agregado debe permanecer dentro de 64 MiB. Los incrementos
se comprueban contra 256 MiB para el coordinador y 128 MiB para Runtime/Engine.

## Configuración final y campaña Golden Axe

La configuración final mantiene colas fijas de 4096 hechos para los productores
3 y 5, 1024 para el productor 4, 384 para el productor 6 y 2048 bloques PCM de
1024 bytes. La reserva agregada medida es 65.202.432 bytes, inferior al límite
B05 de 64 MiB (67.108.864 bytes). El transporte detallado usa `AQF4` para que
las trazas reales de millones de hechos terminen sin omitir campos.

El comprobador prepara para cada consulta y replay una raíz privada de datos de
Runtime, transmite esa raíz mediante el contrato de plataforma y compara una
instantánea completa del árbol ordinario antes y después. La raíz privada se
retira al cerrar, incluso ante error. Los diagnósticos estables son
`runtime_data_isolation_failed` y `user_data_changed`.

La campaña local final `golden-axe-final-qa208r37` acreditó una principal de
7892 cuadros con 14/14 asignaciones seleccionadas y una complementaria de 2130
cuadros con 4 asignaciones seleccionadas y 10 pendientes identificadas. Una
repetición independiente de la complementaria produjo la misma cobertura. Las
tres ejecuciones válidas terminaron con código 0, evidencia reabierta, audio
completo e aislamiento confirmado. Los intentos que usaron una ruta histórica
sin registro de confianza se conservaron como incompletos con
`audio_assignment_catalog_empty`; el comprobador conserva ahora ese diagnóstico
de Runtime en vez de sustituirlo por un desajuste de entradas.

Desde la spec 002 ese caso ya no llega al Runtime: un registro de confianza que falta
se rechaza antes de admitir (`material_not_found: --trust-registry`) y un pack que no
se puede usar, con el motivo de `--probe-pack`. Un pack válido sin catálogo de audio
ya no es un error: la toma se reproduce y registra `assignments=0` (RF-2.1, RF-2.2).

## Cierre local de presupuestos B06, B10 y B13

`audio_qa_fact_throughput` ejecuta la carga sostenida seguida por la ráfaga y
continúa hasta 10.000.000 de hechos; `audio_qa_channel_disconnect_latency`
realiza cien roturas de canal Windows y exige aviso en un máximo de 10 ms. Ambas
forman parte de CTest.

La medición privada B13 se compila como
`audio_qa_private_frame_instrumentation_cost` y no se registra en CTest porque
requiere core, ROM, pack, confianza y toma privados. Recibe esos cinco caminos
posicionales, fija el hilo de medida, realiza calentamiento simétrico y ejecuta
tres parejas con tres repeticiones alternadas por condición. En Windows usa
ciclos efectivos del hilo calibrados. Cada pareja exige P95 encendido ≤1,05×
P95 apagado, diferencia P99 ≤1 ms, conteos de hechos iguales y estado y
decisiones idénticos. Debe ejecutarse una vez por cada toma seleccionada:

```powershell
& '<BUILD>/tests/audio_qa/RelWithDebInfo/audio_qa_private_frame_instrumentation_cost.exe' `
  '<CORE>' '<ROM>' '<PACK>' '<TRUST_REGISTRY>' '<TAKE>'
```

La copia, las colas y la persistencia se miden en B05/B06. B13 mide el trabajo
del productor dentro de `set_input` y `step`, incluida la entrega de todos los
hechos a un contador exacto, separado del pacing.
