# INVESTIGACIÓN — vectorización multi-color y fondos (objetivo 3)

Fecha: 2026-09-12. Fuentes: repo Inkscape (historial completo con
`git fetch --unshallow`), repo Potrace, documentación pública de
vtracer/VisionCortex, ImageMagick, autotrace, y literatura de cuantización.

---

## 1. Inkscape upstream — historia del bug y estado

Hallazgos del `git log` de `src/trace/` (39 commits relevantes):

- **El bug del cuadro blanco viene de 2006** — commit `dd7634180`
  ("refactoring, add background removal", Bob Jamison) introdujo la
  estructura actual de `traceQuant`/`traceBrightnessMulti`: acumulación
  "colores vistos hasta i" + `pop_back()` para el fondo. **Nunca se ha
  corregido la acumulación** en el historial upstream.
- **`ed999e822` (2023) "Unregress stack setting"**: el comportamiento
  "stack" de Inkscape depende del diálogo; en el pipeline el stack solo
  controla el reset del GrayMap (como documentamos).
- **`59d0b04b1` (2015)** fix del estilo `fill:` malformado en brightness
  steps ("mostly black result", launchpad #1456387) — otra señal de que
  esta zona del código es frágil y poco testeada.
- **`3e2303e81` (2020)** crashfix para imágenes grandes; **`a799d924a`
  (2022)** convierte el trazado en asíncrono; **`7c3f7b754` (2022)**
  refactoriza a `/trace` moderno. **Ninguno toca la acumulación stack ni
  el sort de paleta**: nuestro `std::sort(..., ncolor)` sobre entradas
  vacías y el `pop_back()` ciego existen upstream en 1.4.4 tal cual.

**Conclusión:** los fixes IVF de esta iteración (`stack-union`,
`palette-sort`, `prune-nullcheck`, `bg-area`, `alpha-nopremult`,
`invisible-guard`) son contribuciones que upstream no tiene. Son
candidatos razonables a reportar como issues/MRs en GitLab de Inkscape.

## 2. Potrace (Peter Selinger)

- Potrace es **inherentemente binario**: un bitmap de 1 bpp por llamada.
  No existe "multi-layer" en potracelib; el multi-color es siempre
  responsabilidad del llamador (confirmado en potracelib.h: la API es
  `potrace_trace(params, bitmap)` → lista de curvas para UN bitmap).
- Parámetros verificados contra la doc oficial
  (potrace.sourceforge.net): `turdsize` (supresión de manchas, default 2),
  `alphamax` (suavizado de esquinas, 0..1.334, default 1.0),
  `opticurve` (optimización de curvas, default on) y `opttolerance`
  (default 0.2). **Nuestro wiring es correcto** y usa los defaults del
  diálogo de Inkscape.
- El pipeline de referencia (ImageMagick + Potrace) para multi-color es
  exactamente el que implementamos: cuantizar a N colores → umbral por
  color → N llamadas a potrace → apilar SVG. La pregunta de orden/embudo
  de capas es donde ImageMagick no dictamina y donde upstream falla.

## 3. Otras herramientas

### vtracer (VisionCortex, Rust)
- Pipeline: **clustering jerárquico de píxeles por color** (no octree ni
  k-means global), luego trazado de contornos por cluster y ajuste de
  curvas propio. Modos "Cutout" vs "Stacked" (demo oficial).
- En "stacked", vtracer **ordena las capas por tamaño/clustering
  jerárquico** y cada capa superior recorta su geometría al hueco de las
  inferiores — es decir, la semántica de *acumulación invertida* que
  implementamos en `stack-union` es la convención de vtracer: la base
  cubre todo, las capas superiores solo "pintan" lo que les falta.
- Maneja antialiasing con un "gradient step" que agrupa gradientes en
  capas separadas en vez de tratarlos como colores planos (trabajo futuro
  viable para nosotros vía `--scans` alto).

### Adobe Illustrator Image Trace
- Documentación oficial: modos Color/Grayscale/B&W con "palette"
  (limitada a N colores, generada con variantes de median-cut/k-means) y
  paths generados **por región de color, sin solapamiento excesivo**
  (usa relleno por región, no apilamiento de uniones). Ilustrator no
  publica el algoritmo, pero el resultado visible corresponde a tile con
  pequeñas expansiones de solape anti-hendiduras.

### autotrace
- `color_count` + cuantización median-cut interna; el multi-color de
  autotrace genera capas apiladas sin ordenar correctamente en versiones
  antiguas (causa de los resultados erráticos que motivaron retirarlo en
  nuestra v1.1). Sin corrección del problema de fondo conocida.

### ImageMagick + Potrace
- Receta documentada (`convert` + `potrace` encadenados): el usuario debe
  generar un PNG binario por color y apilar manualmente; ImageMagick no
  ordena capas. Confirma que el orden de emisión es el punto débil de
  todos los pipelines basados en potrace binario.

## 4. Cuantización de color (estado del arte)

- **Octree** (Gervautz/Purgathofer, 1988; el que usa Inkscape y nosotros):
  O(n), bueno para paletas pequeñas (2–64), el que usamos.
- **Median-cut** (Heckbert 1982) y su variante MMCQ (Leptonica): más
  simple, tiende a ignorar colores minoritarios.
- **k-means / k-means++** (Celebi 2011 et al.): mejor calidad perceptual
  (PSNR) a coste mayor; es la elección de librerías modernas
  (e.g. `imagequant`/pngquant) y de vtracer (clustering jerárquico ≈
  k-means espacial).
- Literatura: Park & Yoon 2016 (octree mejorado), Frackiewicz 2019
  (k-means sobre imagen reducida). El consenso: para N≤16, octree bien
  implementado es suficiente; el defecto que arreglamos (`palette-sort`)
  era el verdadero destructor de calidad, no el algoritmo base.
- **ΔE CIE-Lab** para el matching de fondo: sigue recomendado como
  trabajo futuro (implementación propia sin dependencias, ~40 líneas);
  la tolerancia RGB 24/canal es un buen compromiso documentado.

## 5. Mejoras aplicables a este proyecto (conclusiones)

Ya implementadas en esta iteración (v1.4):
1. `stack-union`: acumulación invertida (semántica de vtracer "stacked").
2. `palette-sort`: paleta no corrupta (más impacto en calidad que cambiar
   de algoritmo de cuantización).
3. `prune-nullcheck`, `invisible-guard`, `alpha-nopremult`, `bg-tolerance`.

Trabajo futuro priorizado (por impacto):
1. **ΔE CIE-Lab** en `--remove-bg-color`/`remove-border` (precisión
   perceptual del fondo).
2. **k-means opcional** como cuantizador alternativo
   (`--quantizer octree|kmeans`) para gradientes y fotos.
3. **Gradient step** estilo vtracer para imágenes con degradados
   (agrupar bandas de gradiente en capas continuas).
4. Reportar upstream a Inkscape (GitLab) los bugs `stack-union`,
   `palette-sort` y `prune-nullcheck` con los tests de este repo como
   reproducers — es la vía de "devolver" los parches IVF al ecosistema.

## 6. Potrace vs alternativas (resumen)

| Herramienta | Cuantización | Capas | Fondo/alfa | Antialiasing |
|---|---|---|---|---|
| Inkscape 1.4 (upstream) | octree (con bug de paleta) | stack acumulativo **mal ordenado** | compone sobre blanco + pop_back ciego | pre-mezcla con blanco |
| **IVF v1.4 (este repo)** | octree (corregido) | **stack invertido correcto** / tile | **política explícita + umbral de área** | alfa como peso en histograma |
| vtracer | clustering jerárquico | stacked/cutout correctos | respeta alfa | gradient step |
| Illustrator | palette propia (cerrado) | por región | propietario | propietario |
| ImageMagick+potrace | n/a (usuario) | manual | manual | manual |
