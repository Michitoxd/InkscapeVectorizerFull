# FIDELIDAD — pérdida de calidad del SVG y cómo los presets la mitigan

Fecha: 2026-09-12 · v1.5

## Método

1. Vectorizar `examples/sample_input.png` con cada configuración.
2. Renderizar el SVG resultante al tamaño exacto del original (cairosvg,
   fondo blanco para poder comparar con la imagen RGB aplanada).
3. Métrica: distancia RGB media píxel a píxel
   `mean(|original − render|)` (0 = idéntico, 441 = máximo teórico).

## Resultados

| Configuración | distRGB medio | Nodos | Tamaño |
|---|---|---|---|
| Defaults (`--mode color`, scans 8, smooth, stack) | 19.67 | 439 | 73 KiB |
| `--preset logo` (scans 8, opt agresiva) | 19.57 | 992 | 75 KiB |
| `--preset photo` (scans 32, no-smooth, speckles 1) | **17.33** | 3 972 | 580 KiB |
| `--preset high` (scans 48, no-smooth, speckles 0) | **17.12** | 10 531 | 1 031 KiB |
| `--preset photo --tile` | 22.11 | 3 547 | 390 KiB |

## Lectura

- **El stack-union fix es la mayor mejora individual**: con el bug del
  cuadro blanco la distancia era ~240 (imagen tapada). Hoy lo que limita
  la fidelidad es el número de colores (`scans`) y el suavizado.
- **`smooth` es el mayor coste de detalle en fotos** (borra textura fina);
  por eso `photo`/`high` lo desactivan. En line-art/logos hace lo
  contrario: *reduce* artefactos de antialiasing.
- **Tile pierde frente a stack** en fidelidad (22.1 vs 17.3): tile deja
  "escaleres" sin pintar entre bandas de color; stack-union cubre todo
  píxel con su capa correcta.
- **Coste**: `high` cuadruplica nodos frente a `photo` por solo +0.2 de
  mejora. `photo` es el punto dulce para contenido fotográfico.

## Límite estructural (honestidad)

Potrace es un trazador binario: cada capa es negro/blanco. Los
gradientes se aproximan por bandas; el antialiasing no se reproduce.
Esta es la misma limitación de Inkscape. Para gradientes reales el
trabajo futuro es un cuantizador k-means con + bandas dithered, o un
motor de mallas (no planeado para v1.x — ver docs/RECOMENDACIONES.md).
