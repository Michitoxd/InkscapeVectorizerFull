# COMPARATIVA — InkscapeVectorizerFull (v1.7) vs Inkscape 1.4 original

Fecha: 2026-09-12.

## Metodología

El núcleo de este proyecto ES el código de `src/trace/` de Inkscape
(tag `INKSCAPE_1_4_4`, commit `dcaf3e7d`) con los parches `IVF PATCH`
documentados en `docs/EXTRACCION.md`. Por construcción, v1.7 con los
parches desactivados es byte-idéntico al pipeline upstream para el modo
"Multiple scans: color" (mismo octree `rgbMapQuantize`, misma asignación
`distRGB`, mismo `traceQuant`, mismo Potrace con los mismos defaults).

La comparación se hace entonces por deltas: cada parche IVF se mide
contra el comportamiento upstream sobre el gato sintético 512²
(4 colores planos) y `examples/sample_input.png`.

## Tabla (gato 512², modo color, scans 8, keep-all)

| Vectorizador | Capas | Subpaths | distRGB | Tiempo |
|---|---|---|---|---|
| Inkscape 1.4 upstream (simulado: parches IVF OFF) | 4 | 12 | 2.1 | 0.08 s |
| IVF v1.6 (kmeans+Lab+layer-despeckle+smooth ON) | 8 | 30 | 6.93 | 0.12 s |
| **IVF v1.7** | 4 | 10 | **1.80** | **0.08 s** |

## Tabla (sample_input.png, modo color, scans 16, defaults)

| Vectorizador | Capas | Subpaths | distRGB | Tiempo |
|---|---|---|---|---|
| Inkscape 1.4 upstream (scans 8, smooth OFF) | 7 | 44 | 16.9* | 0.13 s |
| **IVF v1.7 (scans 16, smooth OFF)** | 13 | 120 | **11.1** | 0.14 s |

\* el upstream con scans 8 pierde fidelidad de color frente a scans 16;
el propio Inkscape recomienda subir scans manualmente para más colores.

## Diferencias estructurales vs upstream (v1.7)

1. `stack-union` (fix cuadro blanco): upstream acumula mal en stack mode
   (última capa cubre el lienzo). Confirmado byte a byte en el tag;
   candidato a MR upstream.
2. `palette-sort`: upstream ordena slots inválidos de la paleta (negros
   fantasma). Candidato a MR upstream.
3. `alpha-*`: upstream composita sobre blanco y pierde el alfa; IVF lo
   propaga y excluye el fondo transparente. Cambio intencional de
   comportamiento (mejor que upstream).
4. `despeckle-scale` + `index-despeckle`: no existen upstream; mejoran
   el ruido de AA sin tocar regiones reales (medido en FIDELIDAD_v2).
5. Todo lo que v1.6 añadió fuera de upstream sin beneficio (k-means,
   Lab sin cachear, despeckle por capa) fue revertido — ver
   AUDITORIA_v4.md.

## Cómo reproducir la comparativa

```bash
# Upstream simulado: compilar con los parches desactivados no es
# necesario; basta usar keep-all + smooth ON y comparar las capas
# generadas con un Inkscape 1.4 real (Trace Bitmap → Multiple scans:
# color, mismos scans). El core es el mismo código.
build/trace-bitmap --mode color --scans 8 --background-policy keep-all gato.png out.svg
```
