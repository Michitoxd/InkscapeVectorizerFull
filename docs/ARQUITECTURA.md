# Arquitectura — InkscapeVectorizerFull (v1.3)

## Visión general

```
┌────────────────────────────────────────────────────────────┐
│                    Front-ends (embedders)                  │
│   ┌──────────────────┐        ┌──────────────────────┐     │
│   │  CLI             │        │  GUI (GTK3, opcional)│     │
│   │  src/cli/main    │        │  src/gui/main        │     │
│   └────────┬─────────┘        └──────────┬───────────┘     │
└────────────┼─────────────────────────────┼─────────────────┘
                             │        ivf::Settings
                             ▼
┌────────────────────────────────────────────────────────────┐
│              Capa de aplicación (nueva, namespace ivf)     │
│  imageio.cpp   load_image() (gdk-pixbuf, conserva alfa)    │
│  tracer.cpp    Settings → engine wiring (como tracedialog) │
│                + BackgroundPolicy + preview() honesta      │
└───────────────────────────┬────────────────────────────────┘
                            │  TracingEngine::trace/preview
┌───────────────────────────▼────────────────────────────────┐
│         Núcleo extraído de Inkscape (src/core/trace)       │
│  trace.h        TracingEngine, TraceResult, SvgBuilder     │
│  potrace/       Brightness · Quant · Multi-scan            │
│  filterset      gaussian · canny · quantizeBand            │
│  quantize       octree color quantization (alpha-aware)    │
│  imagemap(-gdk) GrayMap/RgbMap/IndexedMap ↔ GdkPixbuf      │
│  async/progress Progress + cancelación                     │
└──────────────────────────┬─────────────────────────────────┘
                           ▼
                 ┌──────────────────┐
                 │    libpotrace    │
                 └──────────────────┘
                           ▼
        ┌──────────────────────────────────────────────┐
        │ lib2geom (paths) · glibmm/gdk-pixbuf (I/O)   │
        └──────────────────────────────────────────────┘
```

## Flujo de datos (trazo)

1. **Entrada**: archivo PNG/JPEG/BMP/… → `ivf::load_image()` (gdk-pixbuf).
   El alfa se preserva tal cual (sin componer sobre blanco).
2. **Configuración**: `ivf::Settings` (dos modos: `Mono` y `Color`, con los
   parámetros del diálogo de Inkscape 1.4: threshold 0.45, scans 8,
   speckles 2, alphamax 1.0, opttolerance 0.2, …) más `BackgroundPolicy`.
3. **Construcción del motor**: `Tracer::make_engine()` replica exactamente el
   wiring de `TraceDialogImpl::getTraceData()` para los modos potrace:
   `PotraceTracingEngine(traceType, invert, quantColors, threshold, floor=0,
   edgeThreshold, scans, stack, smooth, removeBg=false)` + opticurve/
   alphamax/turdsize. La eliminación de fondo ya NO se delega al motor
   (`multiScanRemoveBackground=false`): la hace la fachada con
   `BackgroundPolicy`.
4. **Trazo**: `engine->trace(pixbuf, progress)`. Internamente:
   pixbuf → GrayMap/RgbMap (`imagemap-gdk`, alpha-aware) → filtro
   (brightness/quantize, `filterset`/`quantize`) → potracelib →
   `Geom::PathVector` + string `style`.
5. **Política de fondo** (`tracer.cpp`, IVF PATCH (bg-area)): según
   `BackgroundPolicy` se descarta la capa cuyo color coincide con el fondo
   detectado (borde dominante o color explícito con umbral de área real de
   píxeles). Ver `docs/FIX_FONDO.md`.
6. **Salida**: `SvgBuilder::addPath()` acumula los resultados y
   `getSVG(w,h)` serializa un documento SVG completo (`<svg>` + `<path>`s
   agrupados en `<g>` si hay varios, como hacía Inkscape).

## Preview

`Tracer::preview()` ejecuta **exactamente el mismo filtro** que el trazo real
(brightness en mono; gaussiano + octree en color) pero NO llama a potrace:
rápida y honesta. La CLI la expone con `--preview-out preview.png`; la GUI
la muestra en vivo (incluido el cambio de parámetros).

## Concurrencia (GUI)

- El trazo corre en un `Glib::Thread` worker; la UI nunca se bloquea.
- Progreso: `std::atomic<int>` sondeado por un timeout del main loop.
- Resultado/error: `Glib::Dispatcher` (marshal al main loop de GTK).
- Cancelación: `Async::Progress::keepgoing()` → `CancelledException` en el
  worker; la UI la solicita con un flag atómico.

## Concurrencia

- El núcleo es síncrono y thread-safe por contrato (`trace()` puede llamarse
  desde un hilo worker; upstream ya lo requería).
- Cancelación: `Async::Progress::keepgoing()`; potrace la consulta vía su
  callback de progreso (cuyo ciclo de vida quedó corregido, IVF PATCH
  (progress-lifetime)).

## Decisiones de diseño clave

| Decisión | Motivo |
|---|---|
| Motor síncrono en vez de `TraceFuture` | Elimina dependencia de `Async::Channel` y del modelo de documentos; el embedder elige su modelo de hilos |
| `SvgBuilder` con lib2geom `write_svg_path` | Mismo resultado que `sp_svg_write_path` sin portar `svg/svg.cpp`; un solo punto de serialización |
| Solo dos modos (mono/color) en la fachada | Superficie de bugs mínima; edge detection, steps y autotrace eliminados en v1.1 (ver `docs/AUDITORIA.md`) |
| `BackgroundPolicy` en la fachada, no en el motor | La decisión de qué capas emitir es de política de salida, no de trazado; el núcleo extraído queda intacto |
| Área de fondo medida en píxeles fuente | La geometría trazada de una capa stack siempre cubre ~100 % del lienzo; medir en píxeles es lo semánticamente correcto |
| Sin GUI | Menor superficie de bugs; la CLI es scriptable y fácil de testear |

## Mapa de dependencias (original → standalone)

| Dependencia de Inkscape | Estado |
|---|---|
| SPImage / SPDocument / Selection / DocumentUndo / Inkscape::Pixbuf | **Eliminada** (reemplazada por carga de archivo + SvgBuilder) |
| Async::Channel / TraceFuture / fire_and_forget | **Eliminada** (llamada síncrona) |
| message-stack, dialog-run, builder-utils | **Eliminada** (sin GUI) |
| Inkscape::Drawing / Cairo rasterizado (SIOX) | **Eliminada** (SIOX fuera en v1.1) |
| lib2geom | **Conservada** (enlace dinámico al paquete del sistema) |
| potracelib | **Conservada** (enlace dinámico) |
| autotrace 3rdparty | **Eliminada** (v1.1) |
| glibmm / gdk-pixbuf / gdkmm | **Conservadas** |
| gtkmm (widgets) | **Eliminada** (v1.1) |
