# Informe de extracción — InkscapeVectorizerFull

Proyecto fuente: **Inkscape** — https://gitlab.com/inkscape/inkscape
**Versión usada: tag `INKSCAPE_1_4_4`**, commit `dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871`
(rama estable más reciente en el momento de la extracción, mayo 2026).

Método de localización: `git clone --filter=blob:none --no-checkout` +
`sparse-checkout` de `src/trace`, `src/ui/dialog`, `src/async`, `src/util`,
`src/svg`, `src/3rdparty/autotrace`, `share/ui`. Búsqueda con `git grep` de los
términos: `Trace Bitmap`, `TraceDialog`, `potrace`, `autotrace`, `centerline`,
`brightness`, `edge detection`, `color quantization`, `multiple scans`,
`speckle`, `optimize`, `smooth`.

---

## 1. Archivos extraídos (copia verbatim, salvo cambios de includes anotados)

| Archivo original (Inkscape) | Archivo en este proyecto | Propósito | Cambios |
|---|---|---|---|
| `src/trace/trace.h` | `src/core/trace/trace.h` | Interfaz `TracingEngine`, `TraceResult` | **Reescrito** (ver §2) |
| `src/trace/trace.cpp` | `src/core/trace/trace.cpp` | Tubería asíncrona + inserción en documento | **Reescrito** (ver §2) |
| `src/trace/potrace/inkscape-potrace.h/.cpp` | `src/core/trace/potrace/` | Motor Potrace: brightness, quant, multiscan | Includes adaptados a la nueva estructura de carpetas. Parches: (alpha-quant), (progress-lifetime), (bg-area indirecto) — ver §3 |
| `src/trace/potrace/bitmap.h` | `src/core/trace/potrace/bitmap.h` | Macros de acceso a bitmap de Potrace (de Peter Selinger) | Ninguno |
| `src/trace/imagemap.h/.cpp` | `src/core/trace/` | GrayMap / RgbMap / IndexedMap | Parche (alpha): campo `alpha` paralelo (§3.4) |
| `src/trace/imagemap-gdk.h/.cpp` | `src/core/trace/` | Conversión GdkPixbuf ↔ mapas | Parche (alpha): no compone sobre blanco (§3.4) |
| `src/trace/filterset.h/.cpp` | `src/core/trace/` | Filtros gaussiano y Canny, `quantizeBand` | Ninguno |
| `src/trace/quantize.h/.cpp` | `src/core/trace/` | Cuantización de color por octree | Parche (alpha-quant): píxeles transparentes fuera del histograma (§3.5) |
| `src/trace/pool.h` | `src/core/trace/` | Pool de memoria para el octree | Ninguno |
| `src/async/progress.h` | `src/core/async/progress.h` | `Progress`, `SubProgress`, throttlers, `CancelledException` | Ninguno |
| `src/ui/dialog/tracedialog.cpp` | (referencia, no compilado) | Diálogo original | La lógica de `getTraceData()` está replicada en `src/core/tracer.cpp` |

Eliminados en v1.1 (ya no se extraen/compilan): `src/trace/autotrace/*`,
`src/3rdparty/autotrace/*` (44 archivos), `src/trace/cielab.*`,
`src/trace/siox.*`, `src/util/safe-printf.h` (solo lo usaba autotrace) y la
GUI GTK3 (`src/gui/`). Los parches §3.1–3.3 al autotrace vendido quedaron
obsoletos con su eliminación.

Archivos nuevos (no provienen de Inkscape): `src/core/imageio.*`,
`src/core/tracer.*`, `src/cli/main.cpp`, `tests/test_core.cpp`, sistema de
build y documentación.

## 2. Refactorización de `trace.h`/`trace.cpp` (desacoplo de Inkscape)

El `trace.cpp` original está acoplado al modelo de documento de Inkscape:
`SP_ACTIVE_DOCUMENT`, `SPImage`, `Selection`, `DocumentUndo`, message stacks,
`Inkscape::Pixbuf`, el marco asíncrono `Async::Channel`/`TraceFuture` y GTK
dialogs. Se decidió **no portarlo** sino reemplazarlo por una capa mínima:

| Responsabilidad en Inkscape | Sustituto standalone |
|---|---|
| `TraceTask` + `Async::Channel` + worker threads | El embedder (CLI/GUI) llama `engine->trace()` síncronamente en su propio hilo; `src/core/tracer.cpp` lo encapsula |
| Selección de `SPImage` en el documento | Carga directa de archivo con gdk-pixbuf (`src/core/imageio.cpp`) |
| `TraceTask::do_final_work` (crea `<svg:path>`/`<g>` con XML::Node + undo) | `SvgBuilder` (nuevo, en `src/core/trace/trace.cpp`), que serializa con `write_svg_path()` de lib2geom — el mismo formato que `sp_svg_write_path` produce |
| `Inkscape::Pixbuf` | `Glib::RefPtr<Gdk::Pixbuf>` directamente (misma estructura que consumen los motores) |
| SIOX con rasterizado de formas superpuestas | No incluido: depende del lienzo de Inkscape. `siox.cpp` se conserva compilable para uso futuro |
| `check_image_size` + diálogo de confirmación | `Tracer::image_too_large()`; la decisión queda en el embedder |

La interfaz `TracingEngine` (métodos `trace()`, `preview()`,
`check_image_size()`) **no cambió**: los motores extraídos compilan sin
modificar su lógica.

## 3. Parches al código extraído (todos documentados en el código con el
marcador `IVF PATCH`)

### 3.4 `imagemap.h` / `imagemap-gdk.cpp` — IVF PATCH (alpha) [v1.2]
Los convertidores originales componían la transparencia sobre blanco
(`r = r*a/256 + (255-a)`), convirtiendo todo fondo transparente en una capa
blanca opaca tras la cuantización. Ahora `GrayMap`/`RgbMap` llevan un vector
`alpha` paralelo y los convertidores lo copian tal cual, sin componer.

### 3.5 `quantize.cpp` — IVF PATCH (alpha-quant) [v1.2]
En `rgbMapQuantize`, los píxeles con alfa == 0 no contribuyen al histograma
del octree (los semi-transparentes, ponderados por alfa) y se marcan como
"sin color" en el mapa indexado. `traceQuant` los deja sin trazar en todas
las capas: la transparencia queda como hueco en el SVG.

### 3.6 `potrace/inkscape-potrace.cpp` — IVF PATCH (progress-lifetime) [v1.2]
El callback de progreso de potrace apuntaba a un throttler local de
`grayMapToPath`, quedando colgante tras retornar. Ahora el callback se
restablece antes de salir.

### 3.7 Fachada (`tracer.cpp`) — IVF PATCH (bg-area) [v1.2]
Reemplaza el `pop_back()` ciego de `multiScanRemoveBackground` de Inkscape
por la capa `BackgroundPolicy` (ver `docs/FIX_FONDO.md`). El umbral de área
se mide sobre los píxeles reales de la imagen fuente, no sobre la geometría
de la capa trazada (en modo stack toda capa cubre ~100 % del lienzo por
construcción).

### 3.8 `quantize.cpp` — IVF PATCH (palette-sort) [v1.4]
Upstream ordenaba las `ncolor` entradas del array de paleta aunque solo
`index` fueran válidas (las demás quedan a cero), corrompiendo la paleta
con negros fantasma; `findRGB` además buscaba sobre las entradas nulas.
Fix: ordenar/consultar solo `0..index`. Documentado en
`docs/FIX_CUADRO_BLANCO.md`.

### 3.9 `quantize.cpp` — IVF PATCH (prune-nullcheck) [v1.4]
`octreePrune` desreferenciaba `*ref` antes de comprobar si es nullptr
(crash garantizado con imágenes 100 % transparentes combinado con el
parche alpha-quant). Fix: chequeo primero.

### 3.10 `potrace/inkscape-potrace.cpp` — IVF PATCH (stack-union) [v1.4]
Fix del cuadro blanco: en stack mode la capa i ahora cubre los colores
`i..nrColors-1` (`index >= colorIndex`) en vez de `0..i` (`index ==
colorIndex` sin reset). La capa oscura queda abajo cubriendo todo y las
claras solo su región arriba. El código original es idéntico upstream
desde 2006; ver `docs/FIX_CUADRO_BLANCO.md` y `docs/INVESTIGACION.md`.

### 3.11 `imagemap-gdk.cpp` + `quantize.cpp` — IVF PATCH (alpha-nopremult) [v1.4]
Los convertidores ya no pre-mezclan los píxeles semi-transparentes con
blanco (`r*a/256 + (255-a)`): guardan el color intacto y el alfa se aplica
como peso del histograma del octree (hoja con weight=alpha via
`ocnodeLeafWeighted`). Elimina colores fantasma casi-blancos en bordes
antialiased.

### 3.12 Fachada (`tracer.cpp`) — IVF PATCH (bg-tolerance) [v1.4]
Tolerancia de match de fondo ±8→±24 por canal (`rgb_dist2 <= 3*24*24`),
para fondos antialiased/JPEG (#fdfdfd, #f6f6f6). Variante ΔE CIE-Lab
pendiente como trabajo futuro.

### 3.13 Fachada (`tracer.cpp`) — IVF PATCH (invisible-guard) [v1.4]
Una imagen 100 % transparente puede emitir una capa oscura invisible vía
la ruta brightness del motor; la fachada la descarta cuando hay canal alfa
y cero píxeles visibles.

### 3.14 `imageio.cpp` — IVF PATCH (packed-copy / ga-la-expand) [v1.4]

### 3.15 `potrace/inkscape-potrace.cpp` — IVF PATCH (alpha-sentinel-guard) [v1.5]

**Bug (bloqueante, regresión interactiva):** el parche `stack-union` (3.10)
cambió la condición de capa a `index >= colorIndex`. El centinela de
transparencia `(unsigned)-1` (de `alpha-quant`, 3.5) pasa ese test para
cualquier `colorIndex`, así que cada píxel 100% transparente se pintaba
BLACK en TODAS las capas → el fondo transparente se vectorizaba como un
rectángulo negro opaco (reportado por el usuario en v1.5).
El comentario de `quantize.cpp` "values >= nrColors are ignored there"
era cierto para `==` pero falso para `>=`.

**Fix:** en `traceQuant`, el centinela se comprueba con `==` ANTES de
cualquier comparación y se pinta WHITE (no contribuye a ninguna capa).
Además: (a) el gaussiano de `filterset.cpp` propaga ahora el canal alfa
(IVF PATCH (gaussian-alpha)) para que los píxeles transparentes no se
contaminen con colores vecinos; (b) `IndexedMap::isTransparent()`
nuevo método que consulta el alfa directamente (base para eliminar el
centinela a futuro, ver docs/RECOMENDACIONES.md R1/A1).

### 3.16 `potrace/inkscape-potrace.cpp` — IVF PATCH (mono-smooth) [v1.5]

**Bug (alto):** en modo mono (BRIGHTNESS/CANNY) el parámetro `smooth` no
tenía efecto: `filter()` ignoraba `multiScanSmooth` (solo lo usaba
`filterIndexed()`). La binarización sobre píxeles antialiased crudos
producía contornos espurios que Potrace trazaba como trazos extra junto
a las líneas negras (reportado por el usuario en v1.5).

**Fix:** `filter()` aplica `grayMapGaussian` a petición antes de
binarizar (respeta `invert` igual que upstream). Default: smooth ON en
mono; `--no-smooth` para desactivar.

### 3.17 `potrace/inkscape-potrace.cpp` — IVF PATCH (write-paths-dedup) [v1.5]

**Bug (medio):** el descarte de paths duplicados de `writePaths` usaba
un `unordered_set` del PUNTO FINAL del último segmento compartido por
toda la recursión: descartaba holes legítimos que comparten esquina con
otro contorno (típico en texto y marcos anidados) dejando trazos extra.

**Fix:** la huella de deduplicación es ahora el subpath completo
(secuencia de puntos del curve), con hash combinado, en vez del punto
final aislado.

### 3.18 `filterset.cpp` — IVF PATCH (gaussian-alpha) [v1.5]

Ver 3.15(b): `grayMapGaussian` y `rgbMapGaussian` propagan el vector
alfa en paralelo a los píxeles, para que el suavizado no convierta
zonas transparentes en colores casi-opacos que ensucian la paleta.

### 3.19 `filterset.cpp` + `inkscape-potrace.cpp` — IVF PATCH (mono-despeckle) [v1.5]

**Bug (alto, experiencia de usuario):** puntos negros espurios de 1-16 px
dispersos por la imagen vectorizada (reportado con un SVG real lleno de
subpaths de 1-2 unidades). Cadena de causa:

1. La binarización del filtro brightness deja islas negras de ruido
   (antialiasing, ruido JPEG, residuos de upscaling) mayores que el
   `turdsize` de Potrace (área de contorno, default 2).
2. `turdsize` solo elimina contornos con área ≤ umbral; una mota de
   3×3 px (área de contorno ~12) sobrevive y Potrace la traza como un
   circulito negro.
3. El parche `index-despeckle` (v1.4) ya absorbía estas islas en el
   camino de COLOR (indexed map), pero el modo MONO no tenía ningún
   equivalente pre-trace.

**Fix:** nuevo `grayMapDespeckle(GrayMap, max_island)` en
`filterset.cpp` — etiquetado de componentes 8-conectados sobre la imagen
binaria; las islas menores que `max_island` se repintan con el valor
circundante mayoritario. Se invoca en `filter()` (BRIGHTNESS) tras
binarizar, con el mismo umbral que el camino de color:
`max(12, 4*turdsize)`; `turdsize == 0` (`--speckles 0`) desactiva el
pase. El suavizado `mono-smooth` (3.16) reduce las motas nuevas, pero
el despeckle elimina las que sobreviven al gaussiano.

Verificación: 11 componentes negros → 3 con el parche activo (solo el
rasgo real + las motas ≥ umbral); test de regresión
"mono-despeckle" en `tests/test_core.cpp`.
El repack de rowstride usa `copy()` (antes `scale_simple`, que remuestrea
even a igual dimensión). PNGs G/GA (1–2 canales) se expanden con
`add_alpha()` a RGBA (los convertidores leen p[2]/p[3] y habrían leído
fuera de rango).

## 4. Vacíos documentados

- **Depixelize (pixel-art Voronoi/B-splines):** el diálogo de Inkscape 1.4 lo
  ofrece vía el submódulo `libdepixelize`. No se extrajo: requiere el submódulo
  completo `src/3rdparty/libdepixelize` (Voronoi, N-AvdG…). Trabajo futuro.
- **Modo mono con grises intermedios:** el modo `mono` binariza con el
  umbral de brillo; un gris al 50 % puede clasificarse como blanco y
  desaparecer. Ajustar `--threshold` o usar `--mode color`.
- **Centerline:** el modo de trazado a línea central de autotrace fue
  eliminado con el motor autotrace (v1.1); el núcleo Potrace es binario.
- **SIOX:** eliminado en v1.1 (requería interacción con el lienzo de
  Inkscape).

### 3.20 `imagemap.h` + `quantize.cpp` — IVF PATCH (alpha-threshold) [v1.5.2]

**Problema:** el centinela de fondo se aplicaba solo a píxeles con
`alpha == 0`. Los píxeles del borde antialiased de una región contra
transparencia (alfa 1..127, casi invisibles) se cuantizaban como
contenido sólido: en una imagen con marco oscuro sobre fondo
transparente esto dejaba decenas de motas negras aisladas pegadas al
marco que ningún despeckle por tamaño eliminaba (el propio despeckle
las exentaba por "rodeadas solo de transparencia").

**Fix:** umbral convencional del 50%: `MapBase::ALPHA_THRESHOLD = 128`
y `MapBase::isTransparent()` (ahora también en `MapBase`, compartida
por `RgbMap` e `IndexedMap`). `octreeBuildArea` excluye del histograma
los píxeles con alfa < 128 y `rgbMapQuantize` les pone el centinela.
Verificado en el cubo de 512×512: 93 subpaths-punto → 12; puntos
negros 33 → 0.

### 3.21 `quantize.cpp` — IVF PATCH (palette-merge) [v1.5.2]

**Problema:** los upscalers de IA inyectan jitter de 1-2 por canal en
las zonas planas; el octree dedicaba varias entradas de paleta (y por
tanto capas apiladas completas) a colores casi idénticos
(`#7b4c4c`/`#7d4e4d`/`#814d4d`…), amplificando el ruido en el SVG.

**Fix:** tras ordenar la paleta, se colapsan las entradas que difieren
≤2 por canal (`PALETTE_MERGE_DIST = 2`, deliberadamente pequeño para
no tocar gradientes reales). Con el cubo: 36 → 32 capas reales y
8 849 → 6 426 nodos.

### 3.22 `quantize.cpp` + `filterset.cpp` — circular-despeckle [v1.5.2]

Refinamiento de `index-despeckle`/`mono-despeckle`: la condición de
"speck" ya no es solo el tamaño. Una isla (pequeña o grande) se absorbe
cuando no hay ningún píxel de su propio índice a 1 px de su bounding
box (las franjas de AA que tocan a su región madre son parte del mismo
componente 8-conectado, así que el radio 1 solo protege regiones
cortadas por líneas finas). Isla aislada mayor que 8×max_island se
conserva (probable rasgo deliberado, no ruido).

### 3.23 `quantize.cpp` — IVF PATCH (remove-kmeans / revert-lab-match) [v1.7]

**Reversión de v1.6.** `kmeansRefine` (Lloyd sin separación mínima, tras
palette-merge) y `distLab` en `findRGB` (sin cachear, truncado a int)
rompían zonas planas: centroides casi idénticos + métrica que amplifica
sub-ΔE = islas = motas. Ver AUDITORIA_v4.md §CAUSA-A/B. Estado final:
paleta octree + asignación `distRGB` exactamente como upstream.

### 3.24 `potrace/inkscape-potrace.cpp` — IVF PATCH (remove-layer-despeckle) [v1.7]

**Reversión de v1.6.** `removeSmallBlackIslands` operaba sobre máscaras
parciales stack-union y dejaba píxeles sin capa (huecos). El despeckle
correcto es el del mapa indexado completo + turdsize.

### 3.25 `potrace/inkscape-potrace.cpp` — IVF PATCH (despeckle-scale) [v1.7]

El umbral del despeckle indexado escala con la resolución:
`max(12, 4·turdsize, √(w·h)/20)` → 25 px @512², 51 px @1024².

### 3.26 `cli/main.cpp` — IVF PATCH (smooth-off-color, scans-16) [v1.7]

En modo color: `smooth=false` (como el default del checkbox Smooth de
Inkscape en multicolor) y `scans=16`. Medición en FIDELIDAD_v2.md.
