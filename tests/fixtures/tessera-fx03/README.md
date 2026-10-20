# FX-03 — paquete gráfico sintético

La ROM `fx03_rom.md`, el paquete `fx03.ay` y `trust.toml` se generan bajo
`C:/ayther/AYTHER-Tessera/build/` con `tools/generate-native-fixtures.ps1`,
después de compilar Tessera y `make_test_pack` en Engine. No se publican estos
binarios. El generador fija el `game_id` con el CRC32 de la ROM.
`manifest.json` registra SHA-256 de ROM, pack, registro y assets.

Las dos imágenes de 8×8 píxeles son patrones propios creados para este test,
con licencia CC0-1.0 y autor AYTHER Tessera. La regla apunta al hash medido
del tile rojo visible de la demo, en `(32,32)` a `(39,39)` y de la celda del
plano A en `(96,64)` a `(103,71)`. Hay dos `[[sub]]`
iguales: el catálogo debe elegir `first.png` (magenta); un override de
ejecución sobre el mismo hash debe elegir `second.png` (cian). La máscara
candidata de cambio son únicamente esas dos celdas, y el frame original de
Tessera sigue siendo rojo. La segunda celda tiene prioridad sobre un sprite
verde de SAT slot 0. Otro sprite azul de slot 1 empieza en x=104: slot 0 gana
en x=105 pese a la prioridad gráfica de slot 1, y slot 1 queda visible en
x=109. `manifest.json` fija esos cuatro píxeles originales y los esperados
para la presentación con catálogo y override: magenta o cian en ambas celdas,
verde en x=105 y azul en x=109; fuera de las dos máscaras, bytes idénticos al
original. Engine prueba que el hash del catálogo coincide
con el tile observado, que el paquete firmado carga con la ROM real y que la
primera regla gana.

**Estado:** la ROM, el pack y los oráculos son reproducibles. La prueba local
`ayther.integration.tessera_presentation_test` verifica composición, override,
respaldo, recuperación y conservación de estado/vídeo/PCM originales mediante
Engine–Tessera. La validación del consumo en Runtime corresponde a T031.
