# Changelog

## 1.7 — Reversión de la regresión v1.6 (motas en zonas planas) + defaults fidèles a Inkscape

### Corregido (P0) — regresión v1.6
- **Zonas planas llenas de motas/rayas en modo color** (causa raíz
  triple, docs/AUDITORIA_v4.md):
  - `IVF PATCH (remove-kmeans)`: eliminado `kmeansRefine` — corría
    DESPUÉS de `palette-merge` sin restricción de separación, recreando
    centroides casi idénticos; además gastaba 64 MB
    (`counts(1<<24)`) y 16.8M iteraciones de histograma por llamada.
  - `IVF PATCH (revert-lab-match)`: `findRGB` vuelve a `distRGB`
    (upstream). El Lab sin cachear amplificaba diferencias sub-ΔE
    entre centroides vecinos y partía zonas planas en islas.
  - `IVF PATCH (remove-layer-despeckle)`: eliminado
    `removeSmallBlackIslands` — sobre máscaras parciales stack-union
    deja píxeles sin ninguna capa (huecos), peor que el ruido.

### Cambiado
- **smooth OFF por defecto en modo color** (`smooth-off-color`):
  Inkscape tiene el checkbox OFF en multicolor y la medición lo
  confirma — el gaussiano convierte cada borde duro en una rampa de
  8-9 colores que consume slots de paleta y deja residuo ragged
  (distRGB gato: 6.93 → 1.80). Mono sigue con smooth ON. Opt-in con
  `--smooth` para line-art.
- **scans 16 por defecto en color** (`scans-16`): mejor fidelidad de
  color (sample_input: 16.89 → 11.10) a cambio de más nodos.
- **`IVF PATCH (despeckle-scale)`**: el umbral del despeckle del mapa
  indexado escala con la resolución: `max(12, 4·turdsize, √(w·h)/20)`
  → 25 px @512², 51 px @1024².

### Verificación
- gato 512²: 4 capas exactas, 10 subpaths (antes 8/30), distRGB 1.80
  (antes 6.93), 0 px de speckle interior.
- Tests nuevos: `flat-zone` (región uniforme = 1 subpath) y
  `near-identical` (colapsado sin fragmentación) — reproducen la
  regresión y fallan sin el fix.
- Suite completa: 79 checks PASS.

## 1.5.3 — Fondo transparente en mono, preview con tablero y borde gaussiano

### Corregido (P0)
- **Fondo transparente trazado como rectángulo negro en modo mono**
  (`mono-alpha-composite`): el camino BRIGHTNESS ignoraba por completo el
  mapa de alfa que `alpha-nopremult` le entrega — los píxeles
  transparentes conservaban su RGB crudo (normalmente negro) y se
  binarizaban como tinta. Cualquier PNG con transparencia salía con un
  rectángulo negro de lienzo completo. Fix: componer transparencia →
  blanco ANTES del gaussiano, en `filter()`. Verificado: círculo sobre
  transparencia → 1 único path, sin rectángulo. Test de regresión
  `mono-alpha` añadido.
- **Borde sin filtrar del gaussiano** (`gaussian-border`, R1 de
  RECOMENDACIONES.md): `grayMapGaussian`/`rgbMapGaussian` copiaban
  literalmente una franja de 2 px en los bordes; en imágenes pequeñas se
  veía como borde rasgado y, junto al fix anterior, dejaba tinta sin
  suavizar junto al marco. Fix: convolución con coordenadas reflejadas
  también en los bordes.

### Mejorado
- **Detección de fondo con ruido** (`border-bucket` + `bg-lab`, R2 de
  RECOMENDACIONES.md): `dominant_border_color` agrupaba solo colores
  exactamente iguales — con ruido JPEG (#f4..#fa) el voto se fragmentaba y
  el fondo no se detectaba. Ahora agrupa en cubetas de 16 niveles/canal y
  el candidato es la media de la cubeta. El matching de la capa de fondo
  pasó de distancia RGB (±24/canal) a ΔE perceptual en CIE-Lab (umbral 10).
  Verificado: fondo #f6f6f6 con ruido ±4 se elimina; antes sobrevivía.

### GUI
- **Preview con tablero de transparencia** (`preview-checkerboard`, R4):
  las imágenes con alfa se componen sobre el tablero clásico de 8 px en
  lugar del color del tema del widget — ya no se puede confundir un fondo
  transparente con contenido blanco/negro, y los bordes semitransparentes
  se ven honestos.

## 1.5.2 — Puntos negros de raíz: alpha-threshold, despeckle circular y palette-merge

### Corregido (P0)
- **Puntos negros junto a regiones oscuras sobre transparencia (raíz
  real):** el umbral de alfa era `alpha == 0`, así que los píxeles del
  borde antialiased de una región contra fondo transparente con alfa
  1..127 (casi invisibles) se cuantizaban como contenido SÓLIDO. En la
  imagen del cubo esto dejaba ~50 motas negras aisladas en el borde del
  marco que ningún despeckle por tamaño podía quitar (estaban exentas
  por "rodeadas solo de transparencia"). Fix: `IVF PATCH
  (alpha-threshold)` — corte convencional al 50% (`ALPHA_THRESHOLD =
  128`): alfa < 128 es fondo. Reproducido y verificado: **93 puntos →
  12, negros: 33 → 0**.
- **El despeckle protegía las motas equivocadas** (`circular-despeckle`
  v1.5.2): el radio de exención era `max_island` (12+ px), así que una
  mota que compartía índice de paleta con una región grande cercana (el
  marco negro) se consideraba "parte de una estructura real". Fix: radio
  de exención = 1 px; una franja de AA que toca a su región madre es
  parte del mismo componente 8-conectado y nunca llega a ser isla, así
  que el radio solo protege regiones cortadas por una línea de 1 px.

### Añadido
- `IVF PATCH (palette-merge)`: colapsa entradas de paleta que difieren
  ≤2 por canal (jitter de upscalers de IA tipo `#7b4c4c` vs `#7d4e4d`)
  para que los scans no se gasten en colores casi idénticos. Con el
  cubo: 36 → 32 capas reales, 8 849 → 6 426 nodos.

### Verificación cuantitativa (cube 512×512, 36 scans)
| Métrica | Antes (perfecto.svg) | Después |
|---|---|---|
| Subpaths-punto (<8u) | 93 | **12** |
| Puntos negros | 33 | **0** |
| distRGB vs original | 19.13 | 20.18 (paridad) |
| Nodos totales | ~9 700 | **6 426** |

## 1.5.1 — Despeckle mono: adiós a los puntos negros espurios

### Corregido (P1)
- **Puntos negros espurios en el modo mono:** las islas de ruido
  (antialiasing, JPEG, upscaling) de 3-16 px sobrevivían al `turdsize`
  de Potrace y se trazaban como circulitos negros dispersos. El parche
  `index-despeckle` ya cubría el modo color; faltaba el equivalente
  pre-trace para el modo mono. Fix: `IVF PATCH (mono-despeckle)` —
  nuevo `grayMapDespeckle` en `filterset.cpp`, invocado tras binarizar,
  con el mismo umbral `max(12, 4*turdsize)`. Verificado: 11 componentes
  negros → 3 (solo el rasgo real + motas ≥ umbral).
- Versión del CLI `--version` desactualizada (decía 1.2).

### Añadido
- Test de regresión "mono-despeckle" (con opt-out vía `--speckles 0`).

## 1.5 — Fix del fondo negro + trazos extra + presets de fidelidad

### Corregido (P0)
- **Fondo negro opaco en PNGs transparentes (regresión de 1.4):** el
  centinela de transparencia `(unsigned)-1` pasaba el test `index >=
  colorIndex` del parche `stack-union`, así que los píxeles 100%
  transparentes (RGB negro típico) se pintaban BLACK en todas las capas.
  Fix: `IVF PATCH (alpha-sentinel-guard)` — el centinela se comprueba con
  `==` antes de cualquier comparación y no contribuye a ninguna capa.
  El gaussiano ahora también propaga alfa (`IVF PATCH (gaussian-alpha)`)
  y se añade `IndexedMap::isTransparent()`.

### Corregido (P1)
- **Trazos extra junto a las líneas negras:** en modo mono el parámetro
  `smooth` no tenía ningún efecto (`filter()` ignoraba
  `multiScanSmooth`), así que los píxeles antialiased generaban mini-
  contornos espurios. Fix: `IVF PATCH (mono-smooth)` — suavizado
  gaussiano antes de binarizar, ON por defecto.
- **Holes descartados por deduplicación frágil:** `writePaths` descartaba
  paths que compartían solo el punto final del último segmento (típico
  en texto/marcos). Fix: `IVF PATCH (write-paths-dedup)` — huella del
  subpath completo.

### Añadido
- **Presets de calidad:** `--preset line-art|logo|photo|high`
  (docs/FIDELIDAD.md tiene la tabla cuantitativa; `photo` mejora la
  distancia RGB de 19.7 a 17.3 vs defaults).
- `docs/AUDITORIA_v3.md`, `docs/FIDELIDAD.md`, `docs/RECOMENDACIONES.md`.
- Tests nuevos: guard del centinela (repro del fondo negro) y mono
  smooth (line-art antialiased).

### Documentación
- `docs/FIX_FONDO.md` ampliado con la interacción alpha-nopremult ×
  stack-union que causó el fondo negro.
- `docs/FIX_CUADRO_BLANCO.md` con nota cruzada a la regresión.

## 1.4 — Fix del cuadro blanco + paleta + fondo perceptual

### Corregido (P0)
- **Cuadro blanco sólido en modo color** (bug heredado de Inkscape 2006,
  verificado byte a byte contra upstream): en stack mode el GrayMap
  acumulaba "colores vistos hasta i" sin reset, así que la última capa (la
  más clara) cubría todo el lienzo y tapaba la imagen. La acumulación se
  invierte (`stack-union`): la capa oscura cubre todo (abajo) y las
  claras solo su región (arriba). Medido: distancia RGB al original pasa
  de 240.6 a 11.6.
- **Paleta corrupta**: `rgbMapQuantize` ordenaba/buscaba sobre las
  `ncolor` entradas aunque solo `index` fueran válidas (los ceros se
  convertían en negros fantasma). Solo se ordenan/consultan entradas
  válidas (`palette-sort`).
- **Crash con imagen 100% transparente**: `octreePrune` desreferenciaba
  nullptr antes del chequeo (`prune-nullcheck`) y el motor emitía una capa
  oscura invisible (`invisible-guard` en la fachada).

### Corregido (P1/P2/P3)
- Tolerancia de fondo ±8→±24 por canal (bordes antialiased/JPEG);
  variante ΔE CIE-Lab documentada como trabajo futuro.
- Alfa parcial ya NO se pre-mezcla con blanco: el color viaja intacto y
  el alfa es peso del histograma del octree (`alpha-nopremult`).
- PNGs en escala de grises (G/GA) se expanden a RGBA (evitaba lecturas
  fuera de rango).
- `load_image` usa `copy()` (empaquetado sin remuestrear píxeles).
- La preview de color respeta el alfa (RGBA pixbuf; transparentes ya no
  salen negros).
- GUI: cancela y joinea el worker al cerrar la ventana (evitaba abort a
  la salida mid-trace).
- Preview mono pasa por `engine->preview()` (sin lógica duplicada).
- Flag muerta `--keep-background-rect` eliminada (usar `keep-all`).
- Doble conteo de esquinas en `dominant_border_color` corregido.

### Investigación
- `docs/INVESTIGACION.md`: los bugs confirmados no existen corregidos en
  upstream (39 commits de src/trace revisados); la semántica de
  `stack-union` coincide con la de vtracer "stacked"; candidatos a
  reportar como MRs en GitLab de Inkscape.

## 1.3 — GUI GTK3 restaurada (sobre el núcleo corregido)

### Añadido
- Nueva GUI GTK3 (`trace-bitmap-gui`, `src/gui/main.cpp`): abrir imagen,
  selector de modo (mono/color), parámetros completos, **preview honesta**
  (usa `Tracer::preview()`: el mismo filtro que el trazo, nunca Potrace),
  trazo en hilo de fondo con barra de progreso y cancelación, y exportación
  SVG.
- Compilación condicional: `-DIVF_BUILD_GUI=ON/OFF` (default ON; requiere
  gtkmm-3.0).

### Notas
- A diferencia de la GUI de v1.0 (eliminada en v1.1), esta no usa el marco
  Glade/DialogBase de Inkscape ni un hilo con marshalling frágil: la preview
  vive en la fachada probada y el worker se comunica vía `Glib::Dispatcher`
  + `std::atomic`.

## 1.2 — Bugfix de fondo blanco y transparencia

### Corregido
- **PNG con alfa ya no produce fondo blanco fantasma** (bug heredado de
  Inkscape): los convertidores `gdkPixbufToGrayMap`/`gdkPixbufToRgbMap`
  compozían la transparencia sobre blanco. Ahora el alfa se preserva
  (IVF PATCH (alpha)) y los píxeles totalmente transparentes no
  contribuyen a la cuantización octree (IVF PATCH (alpha-quant)).
- **"Remove background" ya no borra contenido legítimo a ciegas**: el
  `pop_back()` de Inkscape se reemplaza por la capa `BackgroundPolicy`
  con detección por color dominante del borde o por color explícito,
  protegida por un umbral de área real de píxeles (IVF PATCH (bg-area)).
- Puntero de progreso de Potrace colgante (IVF PATCH (progress-lifetime)).

### Añadido
- `--background-policy keep-all|remove-transparent|remove-border|remove-color|auto`
- `--remove-bg-color '#RRGGBB'`, `--background-area-threshold F`
- `--preview-out preview.png` (preview honesta: mismo filtro que el trazo,
  sin Potrace).
- Tests de alfa, políticas de fondo y consistencia de preview.

## 1.1 — Simplificación a dos modos

### Eliminado
- Motor AutoTrace completo y `src/core/3rdparty/autotrace/` (44 archivos).
- SIOX (`siox.{cpp,h}`) y `cielab.{cpp,h}`.
- GUI GTK3 (`src/gui/`) — solo CLI.
- Modos `BrightnessSteps`, `ColorMultipleMono`, `EdgeDetection`,
  `AutotraceSingle`, `AutotraceCenterline`, `AutotraceMultiple`.
- Preprocesado de `imageio` (gamma, grayscale, max-size).

### Añadido
- CLI simplificada: `--mode mono|color` y parámetros de Potrace.
- `docs/AUDITORIA.md` (objetivo 0).

## 1.0 — Extracción inicial

- Extracción del núcleo "Trace Bitmap" de Inkscape
  (tag `INKSCAPE_1_4_4`, commit `dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871`).
- Núcleo Potrace intacto (modos brightness, edge detection, quantization,
  multi-scan), cuantización octree, filtros Canny/Gauss.
- CLI + GUI GTK3, parche del bug de `ignoreColor` del median-cut de
  AutoTrace (IVF PATCH (median-ignorecolor), eliminado con AutoTrace en 1.1).
