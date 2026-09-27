# AUDITORÍA v3 — InkscapeVectorizerFull (v1.4)

Fecha: 2026-09-12 · Build: compila OK, ctest 100 %.
Método: reproducción real de los 3 bugs con imágenes de prueba creadas a
propósito, sondas C++ contra la cadena interna (convertidores → gauss →
octree → potrace), y lectura línea a línea. Nada especulativo salvo lo
marcado.

---

## 0.1 — FONDO NEGRO en PNGs con transparencia (bloqueante, CONFIRMADO)

Reproducción (`/tmp/disk_alpha.png`: disco negro sobre fondo 100 %
transparente con RGB subyacente (0,0,0)):

```
build/trace-bitmap --mode color --scans 4 --background-policy auto /tmp/disk_alpha.png out.svg
→ 1 path: fill:#000000 d="M0 64V0H64 128V64 128H64 0z"  (¡rectángulo completo!)
```

El disco desaparece dentro de un rectángulo negro que cubre todo el
viewBox. Ocurre igual con `auto`, `remove-transparent` y `keep-all`.

### Causa raíz: DOS rutas hacia negro, ambas nuevas respecto a upstream

**Ruta A (`--no-smooth`) — el sentinel se vuelve negro con `>=`:**
`rgbMapQuantize` marca los píxeles transparentes con el centinela
`(unsigned)-1` (= 4294967295). El patch `stack-union` (v1.4) compara
`index >= colorIndex`; el centinela cumple `>=` para TODO colorIndex →
los 11359 píxeles transparentes se pintan `GrayMap::BLACK` en todas las
capas → la capa 0 es un rectángulo negro opaco.

El comentario en `quantize.cpp` — *"values >= nrColors are ignored there
because they can never match a colorIndex loop below"* — era cierto con
el `==` original y **es FALSO desde `stack-union`**. Contradicción
comentario/código confirmada (bug de la v1.4, regresión nuestra).

**Ruta B (`--smooth`, el DEFAULT) — el gaussiano PIERDE el alfa:**
`rgbMapGaussian` (filterset.cpp, línea 71) construye un `RgbMap` nuevo y
**nunca copia el vector `alpha`** (verificado: `rgb1.hasAlpha()==0` tras
el blur). Los píxeles transparentes (RGB crudo (0,0,0) tras el patch
`alpha-nopremult`) entran al octree como negros OPACOS → toda la imagen
cuantiza a negro (16384/16384 píxeles index 0). El disco ni siquiera
aparece.

| Ruta | Config | Mecanismo |
|---|---|---|
| A | `--no-smooth` | sentinel `>=` → BLACK en todas las capas |
| B | default (`--smooth`) | blur pierde alfa → transparente=RGB(0,0,0) opaco → índice 0 (negro) |

Ambas rutas son bloqueantes y explican el reporte del usuario ("antes
blanco, ahora negro"): la v1.2 componía sobre blanco (fondo blanco
fantasma); la v1.4 introdujo estas dos rutas hacia negro.

Fixes (objetivo 1): `alpha-sentinel-guard` en `traceQuant` +
`gaussian-alpha` (copiar/interpolar alfa en `rgbMapGaussian` y
`grayMapGaussian`) + `IndexedMap::isTransparent()` para eliminar el
acoplamiento al centinela.

## 0.2 — TRAZOS EXTRA EN NEGRO (alto, reproducido en modo color; mono OK)

Reproducción (`/tmp/lineas.png`: barras negras finas + diagonal con
antialiasing sobre blanco):

**Modo mono (verificado OK):** el graymap binariza limpio (barras de 2 px
exactas), potrace devuelve 2 paths correctos (bbox 200×3.5 y 2×40),
render limpio. El `writePaths` con `unordered_set` NO descartó nada
legítimo aquí (2 root paths de potrace → 2 subpaths emitidos). La
sospecha 0.2c del brief queda **refutada para este caso** (el descarte
existe y es frágil — ver RECOMENDACIONES — pero no produce los artefactos
reportados en imágenes normales).

**Modo color (AQUÍ están los trazos extra):** con `--scans 6`, la salida
es 6 capas: `#202020`, `#474747`, `#7e7e7e`, `#b0b0b0`, `#c6c6c6`,
`#fefefe`. Los grises intermedios son **anillos/halos de antialiasing**
alrededor de cada trazo negro: el cuantizador convierte el halo AA en
capas propias, que se pintan como contornos paralelos alrededor del
negro. Es exactamente "trazos extras en los trazos de color negro".

Causa raíz: el prefiltrado es binario (o todo, o nada): `--smooth` en
color difumina pero conserva los halos como colores planos; no hay
mecanismo para decir "los píxeles de transición pertenecen al trazo".
Mitigaciones (objetivo 2): suavizado en mono (no lo había: `filter()` de
BRIGHTNESS ignora `multiScanSmooth` — verificado, la sospecha 0.2a del
brief es CORRECTA), presets con menos scans para line-art, y docs.

## 0.3 — PÉRDIDA DE CALIDAD (medida; matiza el reporte)

Distancia RGB media (render cairosvg vs original, sample_input.png 512²,
menor = mejor):

| Config | Dist. | Nodos | KB |
|---|---|---|---|
| defaults (scans 8, smooth) | 8.7 | 213 | 63 |
| scans 16 | 8.6 | 577 | 163 |
| scans 32 | **8.2** | 1002 | 308 |
| scans 32, no-smooth | 9.0 | 2025 | 504 |
| scans 64, no-smooth, speckles 0 | 10.1 | 6397 | 1253 |
| scans 64, no-smooth, speckles 0, no-opt | 10.1 | 7495 | 1454 |

Hallazgos cuantitativos:

1. **Más scans NO mejora la métrica** tras cierto punto (8.2 es el
   mínimo): Potrace binariza cada capa y las capas extra se apilan sin
   ganar detalle; sube el peso 20×.
2. **El gaussiano DAÑA bordes**: midiendo solo píxeles visibles, la
   preview cuantizada da 3.2 de distancia con smooth=OFF vs 8.7 con ON.
   El blur 5×5 (matriz suma 159, correcta) mueve cada píxel de borde
   **46.8** de media (flat: 4.0). En imágenes con alfa además pierde el
   alfa (bug 0.1-B).
3. La pérdida real del flujo viene de: cuantización (8 colores) + blur +
   binarización por capa. La buena noticia: con los defaults el resultado
   YA está a 8.7 del original; el "baja mucho la calidad" del usuario
   corresponde a casos con alfa (bug 0.1) o halos (0.2), no a la métrica
   base.

## 0.4 — Lectura completa: hallazgos adicionales

| Archivo | Línea | Hallazgo | Severidad | Fix |
|---|---|---|---|---|
| filterset.cpp | 71–113 | `rgbMapGaussian` descarta el vector `alpha` | **Bloqueante** (con alfa) | `IVF PATCH (gaussian-alpha)`: copiar alfa (los píxeles del borde sin filtrar conservan el suyo; el interior promedia — el alfa ya es 0/255 en zonas planas) |
| filterset.cpp | 43–68 | `grayMapGaussian` ídem (GrayMap) | Media | mismo patch |
| quantize.cpp | ~575 | comentario "values >= nrColors are ignored" FALSO desde stack-union | Alta | reescribir comentario + `isTransparent()` |
| quantize.cpp | ~561 | centinela `(unsigned)-1` acoplado a `==` implícito en todos los consumidores | Alta | `IndexedMap::isTransparent(x,y)` vía alfa; centinela solo como caché |
| inkscape-potrace.cpp | 398–418 | `traceQuant` pinta centinela BLACK con `>=` | **Bloqueante** | `IVF PATCH (alpha-sentinel-guard)` |
| inkscape-potrace.cpp | 95–140 | `writePaths` dedup por punto final: frágil (contornos que comparten esquina); NO es la causa de los trazos extra en casos normales | Media | `IVF PATCH (write-paths-dedup)`: dedup por huella del subpath completo |
| inkscape-potrace.cpp | 155–168 | `filter()` BRIGHTNESS ignora `multiScanSmooth` → mono sin suavizado (AA→jagged) | Alta | `IVF PATCH (mono-smooth)` |
| filterset.cpp | 85–88 | bordes (2 px) sin filtrar: franja sin suavizar en imágenes pequeñas | Baja | documentado; no tocar (upstream igual) |
| tracer.cpp | 279+ | preview mono re-binariza igual que filter() — tras `mono-smooth` debe pasar por engine->preview() | Media | incluido en mono-smooth |
| tracer.cpp | trace() | `invisible-guard` solo cubre alfa==0 total; con blur sin alfa (bug 0.1-B) no protege | resuelta por gaussian-alpha | — |
| gui/main.cpp | — | sin bugs nuevos detectados en revisión | — | — |

## Resumen de prioridades

| # | Bug | Severidad |
|---|---|---|
| 0.1-A | sentinel→BLACK con `>=` (no-smooth) | Bloqueante |
| 0.1-B | gaussiano pierde alfa (smooth, el default) | Bloqueante |
| 0.2 | halos AA como capas grises (color) + mono sin suavizado | Alta |
| 0.3 | blur daña bordes 46.8/px; métrica ya decente con defaults | Alta (info) |
| 0.4 | comentario falso, centinela frágil, dedup frágil | Media |
