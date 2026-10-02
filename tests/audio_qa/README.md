# Ensayo mínimo de PCM conocido — QA-014

Este ensayo prepara una entrada sintética identificada para las pruebas de captura de plan §5.2 y T7. Cubre RF-10 en la identidad del bloque de entrada y RF-15 en su aislamiento: no enlaza Engine, Runtime ni SDL, no abre un dispositivo y no modifica las reglas de audio. No es una captura ni un doble de la sesión usado para aprobar su comportamiento. QA-015 deberá suministrar esta entrada a la ruta real y observar su salida; ese trabajo no está implementado aquí.

## Bloque `known-pcm-v1`

- PCM S16LE intercalado, 44100 Hz, dos canales en orden izquierda/derecha.
- 1024 sample_frames, rango de entrada `[0, 1024)`, 4096 bytes sin cabecera WAV. No son índices de salida del dispositivo ni cuadros de juego.
- Los primeros cuatro frames son `(-32768, 32767)`, `(0, -1)`, `(8192, -4096)` y `(-16384, 16384)`.
- Para cada índice i desde 4 hasta 1023: izquierda = −8192 + 16i; derecha = 4096 − 4i. Último frame: `(8176, 4)`.
- SHA-256 de los bytes: `5d764a90b6ae1b6606aa95bbd343c54fd1a7b3aa7f7f20e116afc53423081602`.

Las marcas comprueban signo, extremos, orden de canales y endianidad; las rampas permiten identificar desplazamientos sin usar un reloj externo. El valor esperado del hash está fijado independientemente del generador C++. Cambiar la señal exige otra identidad de fixture. `known_pcm.h` proporciona también el bloque tipado, sin E/S ni dependencia de plataforma, para su uso posterior como entrada.

## Ejecutar solo la preparación

Desde la raíz de Runtime, con CMake 3.25 o posterior, C++20 y el toolset Windows del proyecto:

```powershell
cmake -S tests/audio_qa -B out/build/qa-014 -G "Visual Studio 18 2026" -A x64 -T v145
cmake --build out/build/qa-014 --config RelWithDebInfo
ctest --test-dir out/build/qa-014 -C RelWithDebInfo -R '^audio_qa_known_pcm_' --output-on-failure
```

No requiere paquetes adicionales ni red. El target también se incorpora al CMake normal de Runtime, sus flags de calidad y su CTest; los jobs que ejecutan la suite completa incluirán estas pruebas. Con el preset normal previamente configurado:

```powershell
cmake --build --preset windows-ci --target audio_qa_known_pcm_fixture
ctest --preset windows-ci -R '^audio_qa_known_pcm_' --output-on-failure
```

Las pruebas son `audio_qa_known_pcm_block` y `audio_qa_known_pcm_errors`. La primera ejecuta dos veces el generador y comprueba formato, tamaño y hash contra valores fijos. La segunda exige rechazo de argumentos ausentes, destino no escribible y archivo ya existente, conservando un archivo centinela. Un fallo devuelve código no cero y bloquea CTest; no se usa un skip si no se produce el bloque.

Cada ejecución conserva un subdirectorio nuevo bajo `results/<config>/` del directorio de build de este ensayo. El test informa la ruta a `first.toml` y conserva `first.s16le.pcm`, `repeat.s16le.pcm` y sus manifiestos. El manifiesto se escribe solo después de releer y verificar el PCM. Los resultados son fixtures regenerables; no implementan la conservación transaccional de evidencia de sesiones prevista en T11. Si se interrumpe una escritura puede quedar un archivo parcial sin aceptación del test; no se reutiliza como resultado válido.

El ejecutable acepta una única ruta nueva de salida, escribe el PCM y emite sus metadatos TOML por stdout. Rechaza reemplazar archivos existentes. Está destinado a directorios de ensayo controlados y sin escritores concurrentes; no constituye la interfaz del comprobador ni su mecanismo de reserva de archivos. El caso normal no necesita ROM, pack, core ni tomas privadas.

Verificación local de formato: clang-format 22.1.6, estilo LLVM con `IndentWidth: 4` y `ColumnLimit: 100`, aplicado solo a los archivos nuevos. Análisis: clang-tidy con la configuración `.clang-tidy` de Runtime y cabeceras del toolset utilizado. Compilación con warnings tratados como errores. La prueba de captura postmix, la alineación de salida, las rutas auxiliares, el silencio, el cierre y la fidelidad de la instrumentación siguen pendientes; este ensayo no los acredita.
