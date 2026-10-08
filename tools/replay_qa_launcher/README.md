# Launcher de replay QA (`ayther_replay_qa`)

`ayther_replay_qa` es la ventana para preparar y lanzar una comprobación de replay QA sin escribir la línea de órdenes de `ayther_audio_qa check`. Usa la misma biblioteca, `run_check`, y la misma tabla de opciones: cada opción de `check` tiene un campo en el launcher, y ninguna opción queda fuera.

Se instala con el componente `qa`, junto a `ayther_audio_qa`, y nunca con el componente de usuario final del Runtime.

## Uso

1. **Abre `ayther_replay_qa.exe`.**
   - La ventana empieza vacía: no recuerda la ROM, las tomas ni el pack de una sesión anterior.
   - La modalidad inicial es la presentación visible.
2. **Elige los materiales** con «Elegir…», que abre el diálogo nativo de archivos.
   - **ROM:** el juego.
   - **Tomas:** una o varias `.ayr`. Se pueden reordenar, repetir y quitar; cada repetición se ve como otra posición.
   - **Pack:** opcional. «Quitar» lo retira, y el resumen dice entonces «Sin pack».
   - **Runtime y core:** si hay un Runtime instalado junto al launcher y una configuración de AYTHER Play CE, salen de ahí. El resumen muestra su origen como «entorno», y la configuración de Play CE sólo se lee.
   - **Rutas pegadas:** una ruta pegada entre comillas dobles, como la deja «Copiar como ruta de acceso» de Windows, se usa sin ese par de comillas en todos los campos de ruta. El resumen y la solicitud registran la ruta sin comillas.
3. **Revisa las secciones.** Son entorno y condiciones, información auxiliar, destino e identidad, presentación e idioma. Un campo no válido muestra su error debajo, por ejemplo «Campo obligatorio» o el código de la validación previa, como `core_platform_mismatch`.
4. **Comprueba el resumen de valores efectivos.** Cada fila muestra su origen: seleccionado, por defecto, entorno o manifiesto.
   - Cualquier cambio vuelve a validar la solicitud con el Runtime antes de poder iniciar.
   - «Iniciar» sólo se habilita cuando no queda ningún error.
5. **Pulsa «Iniciar».**
   - La solicitud corre en un hilo de trabajo y la ventana no se bloquea.
   - La línea de fase muestra validación, admisión, preparación, reproducción y cierre, con la toma en curso.
6. **Para detener, pulsa «Cancelar».** Cancela la solicitud y las tomas que no empezaron quedan como no iniciadas.
7. **Lee los resultados.** Al terminar se ve, para cada toma, la reproducción, el recorrido y la evidencia, con los motivos de una evidencia incompleta. Debajo aparece el resultado conjunto. Un resultado técnico no es una evaluación visual.

### Idioma

- La interfaz está en español o en inglés según el campo «Idioma», que es la opción `--language` (`es` por defecto, `en`).
- El mismo idioma llega a la ventana de replay del Runtime: avisos, overlay de depuración y mensajes.

### Controles de la ventana de replay

Con presentación visible, el Runtime abre su propia ventana por toma. Los controles son:

- **Espacio:** pausa al terminar el frame en curso (si no hay ninguno, en el último mostrado) y reanuda.
- **← y →:** en pausa, retroceden y avanzan un frame.
- **I:** muestra u oculta el registro de depuración.

Están descritos en `docs/input-map.md` y en `docs/audio-qa-evidence.md`.

## Compilación

El launcher forma parte de la compilación QA del Runtime y requiere el paquete QA del Engine (ver `docs/development.md`, «Replay QA»):

```powershell
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine_qa.ps1
cmake --preset windows-qa
cmake --build --preset windows-qa --target ayther_replay_qa
```

- **Dependencias.** SDL3 y Dear ImGui, con el backend SDL_Renderer de ImGui. Por eso `vcpkg.json` activa la feature `sdl3-renderer-binding` de `imgui`, además de `sdl3-binding` y `vulkan-binding`. La ventana del launcher dibuja con SDL_Renderer y no necesita Vulkan.
- **Estructura.** El modelo es puro y se compila aparte, en la biblioteca `ayther_replay_qa_model`: formulario, validación, secciones, resumen, resultados y textos. La ventana (`launcher_app.cpp`) sólo dispone ese modelo.

## Pruebas

Sin GPU:

```powershell
ctest --preset windows-qa -R "^replay_qa_launcher_" --output-on-failure
```

- **`replay_qa_launcher_launcher_messages`:** catálogo es/en.
- **`replay_qa_launcher_launcher_model`:** campos, obligatorios, «Sin pack», tomas ordenadas, revalidación, fases y resultados.
- **`replay_qa_launcher_environment_resolver`:** Runtime y core del entorno.
- **`replay_qa_launcher_launcher_runner`:** hilo de trabajo y cancelación.
- **`replay_qa_launcher_launcher_view`:** diálogos, secciones, validación por tipo, resumen, errores y resultados.
- **`replay_qa_launcher_option_coverage`:** un campo por opción. Regenera `evidence/rf1-option-correspondence.md` en el árbol de compilación.

Con GPU y escritorio (`windows-qa-gpu`):

```powershell
ctest --preset windows-qa-gpu -R "^replay_qa_launcher_(window_smoke|interface_smoke|idle_cpu)$" --output-on-failure
```

- **`replay_qa_launcher_window_smoke`:** `ayther_replay_qa --smoke-frames 30` abre la ventana y dibuja 30 fotogramas. `--smoke-capture <archivo.bmp>` guarda el último.
- **`replay_qa_launcher_interface_smoke`:** `ayther_replay_qa --self-test` comprueba la interfaz de punta a punta:
  - muestra los errores de los campos obligatorios;
  - inicia una solicitud visible con el core sintético;
  - la cancela tras dos segundos de reproducción;
  - muestra la toma como «Cancelada» y el resultado conjunto.
  - Los materiales llegan con `--runtime`, `--core`, `--rom`, `--take` y `--output`.
- **`replay_qa_launcher_idle_cpu`:** `ayther_replay_qa --idle-seconds 3` deja la ventana en reposo tres segundos e informa de los redibujos y de la CPU usada. En reposo la ventana espera la entrada (o redibuja cada 100 ms el progreso) con vsync, para no competir con el Runtime mientras presenta la reproducción; la prueba exige como mucho un redibujo por refresco y menos de un cuarto de núcleo.

`--self-test`, `--smoke-frames`, `--smoke-capture` e `--idle-seconds` son modos de prueba del ejecutable QA; no forman parte del uso normal.

## Campos precargados (DI-23)

`ayther_replay_qa --prefill <archivo>` abre el formulario con los campos del archivo: una
línea `--campo=valor` por valor; `--take` y `--core-option` se repiten en orden. Todos los
valores cuentan como explícitos y los campos ausentes quedan sin seleccionar. Una línea
desconocida o mal formada rechaza el archivo entero (`prefill_rejected` en el log) y el
formulario queda vacío. Lo usa la herramienta de campaña de la spec 002.
