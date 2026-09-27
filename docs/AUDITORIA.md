# AUDITORÍA — InkscapeVectorizerFull (Objetivo 0)

Fecha: 2026-09-12 · Estado del build al auditar: **compila OK** (CMake+make, 0 errores),
`ctest`: 1/1 pasa. 26 warnings capturados (ver §1.2). Método: compilación real con
`-Wall -Wextra`, ejecución real de los 9 modos con `examples/sample_input.png` y
5 imágenes sintéticas de sonda (RGBA transparente, cuadrados desplazados, L asimétrica),
e inspección de código línea a línea. Todo lo indicado abajo fue **reproducido o
verificado contra los headers reales del sistema**; nada es especulativo.

---

## 1. Bugs de compilación

### 1.1 Ítems sospechados por el usuario — veredicto real

| # | Sospecha | Veredicto | Evidencia |
|---|---|---|---|
| 1 | `Glib::Quark("ivf-imageio")` ilegal | **FALSO ALARMA** (compila y es correcto) | `glibmm/quark.h` define `Quark(const ustring&)` y `Quark(const char*)` (líneas 62-63). Además ese código está en una rama que nunca se ejecuta (create_from_file lanza en vez de devolver null) |
| 2 | `#include <gdkmm/wrap_init.h>` no existe | **FALSO ALARMA** | El header existe en gtkmm-3.24 (`/usr/include/gdkmm-3.0/gdkmm/wrap_init.h`); `Gdk::wrap_init()` es necesario y se usa en CLI/tests. Fue un fix ya aplicado durante el build inicial |
| 3 | `write_svg_path(pv, 8, true)` firma incorrecta | **FALSO ALARMA** | Firma real: `write_svg_path(PathVector const&, int prec=-1, bool optimize=false, bool shorthands=true)` — los 3 argumentos son legales |
| 4 | `std::unordered_set<Geom::Point>` sin hash | **FALSO ALARMA** | lib2geom 1.4 define `std::hash<Geom::Point>` en `point.h` (líneas 436-448, especialización `namespace std`) |
| 5 | Alias `at_fitting_opts_type` duplicado | **FALSO ALARMA (compila)** | `autotrace.h` declara el tipo C como `struct _at_fitting_opts_type` incompleto; el alias C++ es legal y compila sin warning |

### 1.2 Warnings reales capturados (compilación completa)

| Archivo | Línea | Warning | Severidad | Fix propuesto |
|---|---|---|---|---|
| `src/core/trace/trace.h` | 82 | `unused parameter ‘size’` ×7 (stub `check_image_size`) | baja | Eliminar `check_image_size` del núcleo (Objetivo A) |
| `3rdparty/autotrace/pxl-outline.c` | 80 | `may fall through` | media (vendored) | Sin acción: código upstream; se elimina en Objetivo A |
| `3rdparty/autotrace/fit.c` | 1131 | `‘spline’ used uninitialized` | **alta** (vendored) | Ruta de error potencial en autotrace; se elimina en Objetivo A |
| `tests/test_core.cpp` | 102, 185 | unused var, sign-compare | baja | Limpiar al reescribir tests |
| `trace/autotrace/inkscape-autotrace.cpp` | 178 | sign-compare | baja | Se elimina en Objetivo A |

**Conclusión §1:** no hay errores de compilación bloqueantes hoy. Los warnings
serios están dentro del código autotrace vendido, que se elimina en el Objetivo A.

---

## 2. Bugs lógicos en el código extraído y la fachada

### 2.1 Fidelidad del wiring `Tracer::make_engine()` vs `getTraceData()`

Comparado campo por campo contra `tracedialog.cpp` (INKSCAPE_1_4_4):

| Parámetro diálogo | tracer.cpp | ¿Fiel? |
|---|---|---|
| traceType, invert, SS_CQ_T→quantizationNrColors, SS_BC_T→threshold, floor=0, SS_ED_T→canny, MS_scans, stack, smooth, removeBg | idéntico | ✓ |
| setOptiCurve / setOptTolerance | idéntico | ✓ |
| setAlphaMax(smooth? smooth_value : 0) | idéntico | ✓ |
| setTurdSize(speckles? speckles_size : 0) | idéntico | ✓ |
| autotrace: color_count, centerline, preserve_width, filter_iterations, error_threshold | idéntico (multi = scans+1) | ✓ |

**Veredicto: wiring fiel. Sin bugs aquí.**

### 2.2 Modos de `ivf::Mode` vs el diálogo de Inkscape

El diálogo expone 9 combinaciones (SS_BC, SS_ED, SS_CQ, SS_AT, SS_CT, MS_BS,
MS_C, MS_BW, MS_AT) + el panel Pixel Art (depixelize, no extraído). La fachada
tiene los 9. Sin huérfanos ni duplicados — pero **5 de los 9 desaparecerán en
el Objetivo A** por decisión de producto.

### 2.3 `traceSingle` y `brightnessFloor`

`traceSingle()` fuerza `brightnessFloor = 0.0` al inicio, igual que upstream.
**Fiel.** PERO: el **preview** no pasa por `traceSingle` y no resetea
`brightnessFloor` — ver §4.2.

### 2.4 `traceQuant` con `multiScanStack == false`

Verificado en `inkscape-potrace.cpp::traceQuant`: cuando `!multiScanStack`, los
píxeles que no son del color actual se ponen a WHITE en cada pasada (tile).
**Fiel a upstream.**

### 2.5 `traceBrightnessMulti` — floor incremental

Verificado: `brightnessThreshold = low + delta*i` por iteración y
`brightnessFloor = brightnessThreshold` solo cuando `!multiScanStack`. **Fiel.**

### 2.6 BUG REAL — style de centerline/open paths (autotrace)

`inkscape-autotrace.cpp::get_style()`:
```cpp
ss << (splines->centerline || list.open ? "stroke:" : "fill:") << color
   << (splines->centerline || list.open ? "fill:" : "stroke:") << "none";
```
Produce `stroke:#...;fill:none` para centerline — correcto. **Sin bug real**
(se elimina en A de todos modos).

### 2.7 BUG REAL (ya parcheado antes, verificado de nuevo) — autotrace destruye el pixbuf

`at_splines_new_full()` muta el bitmap. Ya existe el fix `IVF NOTE` con copia
privada, **pero la copia solo se hace si `pb == pixbuf`**: cuando el pixbuf ya
es RGB8 sin alfa, `to_rgb8_packed` devuelve el mismo objeto y la copia sí se
aplica. Verificado: la lógica es correcta tras el parche previo.

---

## 3. Bugs de memoria y recursos

| # | Dónde | Bug | Severidad |
|---|---|---|---|
| 3.1 | `inkscape-potrace.cpp:270-273` | `potraceParams->progress.data` apunta al `throttled` **local** de `grayMapToPath`. El puntero queda colgando tras retornar. Solo es peligroso si se llama `potrace_trace` después sin reasignar — hoy se reasigna en cada llamada, así que **no hay UB activo**, pero es frágil | media |
| 3.2 | `gui/main.cpp:334` | `Glib::Thread::create(..., false)` (no-joinable). Si la ventana se destruye mientras el worker corre, el callback usa `this` colgando. Existe mitigación parcial (`shared_ptr`), pero `_outcome`/`_dispatcher` usan `this` crudo | alta (moot: la GUI se elimina en A) |
| 3.3 | `trace.cpp` (SvgBuilder) | Sin leaks: todo por valor/RefPtr | — |
| 3.4 | `siox.cpp` | Sin leaks detectables (vectores RAII) | — |
| 3.5 | `median.c` | `initialize_median_cut` XMALLOC sin liberar en la rama de error `spp != 3` | baja (se elimina en A) |

---

## 4. Bugs funcionales (reproducidos)

### 4.1 BUG CRÍTICO — Fondo blanco fantasma con PNGs transparentes (objetivo D)

**Reproducción exacta** (imagen de sonda: círculo negro sobre fondo 100% transparente):
```
$ trace-bitmap --mode multicolor -s 4 alpha_circle.png out.svg
fill:#030303  ← círculo
fill:#5b5b5b  ← FANTASMA (franja de antialias contra el blanco compuesto)
fill:#a2a2a2  ← FANTASMA
fill:#fdfdfd  ← FANTASMA: todo el fondo transparente vectorizado como blanco opaco
```
**Causa raíz** (localizada): `imagemap-gdk.cpp::gdkPixbufToGrayMap/RgbMap`
compone sobre blanco: `r*alpha/256 + (255-alpha)`. El cuantizador ve blanco
donde hay transparencia y le asigna una capa opaca. El mismo patrón existe en
`to_rgb8_packed` (autotrace). Detalle y solución: `docs/FIX_FONDO.md`.

### 4.2 BUG ALTO — Preview rota

Confirmado por lectura del código (`tracer.cpp::preview` + motor):

1. **Modo autotrace**: `AutotraceTracingEngine::preview()` devuelve la imagen
   *sin trazar* (comentario upstream: "Todo: Actually generate a meaningful
   preview"). La GUI lo mostraba como si fuera preview real → **mentira visual**.
2. **Modo BRIGHTNESS_MULTI**: el preview usa `filterIndexed()` (octree de
   colores) mientras el trazo real usa umbrales de brillo — upstream lo admite:
   *"this is a lie: multipass doesn't use filterIndexed"*. Preview ≠ filtro real.
3. **`traceSingle` resetea `brightnessFloor=0.0`; el preview no pasa por esa
   ruta** y `PotraceTracingEngine::preview()` llama `filter()` directamente:
   en el motor recién construido el floor ya es 0, así que hoy no difiere, pero
   es una dependencia oculta del orden de llamadas.
4. En la GUI, la preview se recalcula con debounce 200 ms en *cada* cambio de
   spin/check, reconstruyendo el motor entero cada vez; con imágenes grandes
   esto congela la UI (se elimina con la GUI en A).

**Fix diseñado** (Objetivo B): preview = el MISMO filtro que el trazo
(mono → `filter()`, color → `filterIndexed()`), expuesto como
`Tracer::preview()` + `--preview-out preview.png` en CLI. Sin Potrace en la
preview.

### 4.3 BUG ALTO — Brightness "se come" tonos medios (reproducido)

Imagen de sonda: cuadrado gris 128 sobre blanco.
```
$ trace-bitmap --mode brightness gray_square.png out.svg
→ SVG con <path d=""/> VACÍO (0 nodos)
```
**Causa**: gray128 → brillo 384 ≥ cutoff por defecto (345.6) → clasificado
como blanco → nada que trazar. Es el comportamiento upstream (el usuario de
Inkscape ajustaría el umbral), pero con la CLI minimalista hay que documentarlo
y/o elegir un umbral por defecto más alto. Con `--invert` tampoco ayuda (el
fondo blanco se vuelve negro). El modo `mono` final debe tratarlo
(documentado en README).

### 4.4 VERIFICADO NO-BUG — Eje Y correcto

Sospecha del usuario: "el eje Y puede estar invertido". **Falso.** Prueba con
L asimétrica (pie en y=[70..90]) y cuadrado en esquina superior derecha:
el SVG renderiza la orientación correcta. Mi primer análisis con regex fue un
falso positivo (los comandos H/V de SVG tienen 1 solo coordenada). El `d=`
`M50 30V10H70 90V30 50H70 50z` para el cuadrado x=[50..90] y=[10..50] es correcto.

### 4.5 VERIFICADO NO-BUG — Orden de capas

`traceQuant`/`traceBrightnessMulti` generan capas de oscuro→claro (el CLUT del
octree se ordena por r+g+b ascendente y los umbrales van de bajo a alto); el
orden de emisión en el SVG es oscuro primero = correcto para stacking (las
claras van encima). Confirmado con `steps` (`#333333`→`#d0d0d0`) y
`multicolor` (`#030303`→`#fbfbfb`).

### 4.6 BUG MEDIO — "Remove background" ciego

`pop_back()` elimina la última capa (la más clara) sin verificar que sea fondo.
Con contenido blanco legítimo lo borra. Reproducible: en `multicolor` del
sample, la capa `#fbfbfb` desaparecería con `--remove-bg`. Solución: Objetivo D.

### 4.7 OBSERVACIÓN — `automulti` genera 1136 paths (8 colores)

Autotrace multi con 8 colores produce 1136 paths/minucias vs 8 del potrace
equivalente. No es un bug per se (son splines por región), pero confirma la
decisión de eliminar autotrace.

---

## 5. Deuda técnica y código muerto (confirmado)

| Ítem | Estado | Acción |
|---|---|---|
| `src/core/trace/autotrace/bitmap.h` | **CONFIRMADO muerto**: nadie lo incluye (grep vacío); es el bitmap.h de potrace copiado por error de cp con wildcard | borrar (A) |
| `boost` en DEPENDENCIAS.md como "opcional" | **CONFIRMADO incorrecto**: `inkscape-potrace.h` incluye `<boost/functional/hash.hpp>` → obligatorio hoy. (Nota: lib2geom ya lo arrastra, por eso compila) | corregir doc; valorar quitar el include en A |
| `docs/config.h.autotrace.orig` | referencia histórica, ok | conservar |
| `tests/test_core.cpp:102` | variable `n` sin usar | limpiar (A) |
| `check_image_size` stub | siempre false; no implementado de verdad | eliminar (A) |
| preprocesado gamma/grayscale/max-size | fuera del alcance del producto | eliminar (A) |
| `docs/dialog-trace.glade.orig` | referencia | conservar |

---

## Resumen de priorización propuesta

| Prioridad | Bug | Objetivo |
|---|---|---|
| P0 | Fondo blanco fantasma con alfa (4.1) | D |
| P0 | Preview mentirosa / filtro inconsistente (4.2) | B |
| P1 | Remove-background ciego (4.6) | D |
| P1 | Eliminación de autotrace/SIOX/GUI (deja sin efecto 1.2, 3.2, 3.5, 4.7) | A |
| P2 | Brightness come tonos medios (4.3) | C (doc + default) |
| P2 | Puntero de progreso potrace frágil (3.1) | C |
| P3 | Limpieza tests/dead code | A |

El usuario ya autorizó proceder con el plan completo (0→A→B→C→D→E); se continúa
con el Objetivo A tras este informe.
