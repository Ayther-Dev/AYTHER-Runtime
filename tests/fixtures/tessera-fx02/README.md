# FX-02 para Runtime

`program.md` es la ROM de 32 KiB generada bajo `build/` por `tests/backend_probe/main.cpp`
de AYTHER Tessera, bajo MIT. La fuente y el oráculo están en
`tests/fixtures/audio-active/` del repositorio AYTHER Tessera. El manifiesto de referencia
identifica el contenido y su generador; el binario no se distribuye. SHA-256 de la ROM:
`5296c4fa331fd8cb47663fc7ce3b0abef03364221b309a65f2966c6a8642225c`.

La prueba de dispositivo mantiene B pulsado durante 180 frames y exige PCM
original no silencioso entregado al stream SDL. Los oráculos de transiciones
neutral/activo/neutral, estado, vídeo y hashes se verifican en Tessera y Engine.
