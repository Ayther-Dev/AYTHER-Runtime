# Formato de hechos de QA de audio

Los mensajes `fact_batch` usan el protocolo binario 1.0 de Runtime. Su carga
comienza con un contador `uint32` little-endian y contiene registros precedidos
por su longitud `uint32` little-endian. Un registro no puede superar 64 KiB; un
lote no puede superar 256 KiB ni 1024 registros. Estos límites se validan antes
de reservar memoria.

El lector admite estos esquemas de registro:

| Esquema | Representación | Uso |
| --- | --- | --- |
| 1.0 | TOML completo | Formato histórico con todos los campos de `Fact`. |
| 1.1 | TOML compacto | Hechos sin causas cuyos cinco campos opcionales son `not_applicable` y `compact_default`. |
| 1.2 | TOML detallado | Campos tipados representados como tablas. |
| 1.3 | TOML detallado compacto | Los mismos campos tipados en arreglos compactos. |
| 1.4 | Binario `AQF4` | Representación vigente para hechos detallados. |

El escritor usa 1.1 para el caso compacto sin campos y 1.4 cuando el hecho
contiene campos detallados. Los formatos 1.0–1.3 permanecen legibles para
reabrir evidencia ya publicada. Una versión desconocida, un valor fuera de
rango, una longitud incoherente o bytes residuales se rechazan; el lector no
infiere valores predeterminados de otro esquema.

`AQF4` comienza con su magic, versión 1.4 y longitudes explícitas. Codifica en
little-endian la identidad del hecho, productor y secuencia, cuadro, clase,
causas, identidades opcionales, motivo, órdenes compartidos y todos los campos
tipados. Las cadenas llevan longitud y se validan antes de copiarlas. La
decodificación reconstruye exactamente el mismo modelo `Fact`; no modifica la
causalidad, la disponibilidad ni los motivos.

La variante 1.1 permite la carga reproducible de 256 bytes por registro usada
por B06. La variante 1.4 reduce el coste de las trazas reales con millones de
campos sin rebajar los límites ni omitir datos. El framing exterior, sus hashes
y los contadores de emisión, recepción y durabilidad se verifican por separado.
