# Informe final de ingeniería — InkscapeVectorizerFull (v1.5)

## 1. Resumen ejecutivo

- **v1.0**: extracción funcional de "Trace Bitmap" de Inkscape
  (`INKSCAPE_1_4_4`) como aplicación autónoma (CLI + GUI).
- **v1.1**: simplificación radical — solo dos modos (`mono`, `color`);
  eliminados AutoTrace, SIOX, CIE-Lab, edge detection, steps y la GUI.
- **v1.2**: corrección del bug histórico de fondo blanco/transparencia
  (`BackgroundPolicy`, alfa preservado; `docs/FIX_FONDO.md`).
- **v1.3**: GUI GTK3 restaurada sobre el núcleo corregido, con preview
  honesta y trazo en hilo de fondo.
- **v1.4**: corrección del **bug del cuadro blanco** (stack mode tapaba la
  imagen con la capa más clara — bug heredado de Inkscape 2006, verificado
  byte a byte contra upstream), de la **paleta corrupta** del octree y del
  crash con imágenes 100 % transparentes; tolerancia de fondo ±24/canal;
  alfa parcial sin pre-mezclar; PNGs G/GA; preview RGBA; GUI-join;
  limpieza de flag muerta y esquinas. Auditoría v2 en
  `docs/AUDITORIA_v2.md`; mecanismo y medición en
  `docs/FIX_CUADRO_BLANCO.md`; investigación upstream/herramientas en
  `docs/INVESTIGACION.md`.
- **v1.5**: corrección de la **regresión del fondo negro** (el centinela
  de transparencia `(unsigned)-1` pasaba el test `>=` de `stack-union` y
  los píxeles transparentes se pintaban BLACK en todas las capas —
  `docs/FIX_FONDO.md` §5); **trazos espurios junto a las líneas negras**
  en modo mono (`smooth` no se aplicaba al filtro de brightness; la
  deduplicación de `writePaths` descartaba holes por punto final);
  **presets de calidad** `--preset line-art|logo|photo|high` con medición
  cuantitativa (`docs/FIDELIDAD.md`); recomendaciones de mantenimiento en
  `docs/RECOMENDACIONES.md`. Auditoría v3 en `docs/AUDITORIA_v3.md`.

## 2. Repositorio y versión usada

- Fuente: https://gitlab.com/inkscape/inkscape
- **Tag: `INKSCAPE_1_4_4`** — commit `dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871`
  (2026-05-05). Clonado blobless + sparse-checkout (no se copió Inkscape
  completo).

## 3. Estado actual del código

- **Conservado sin reescribir** (núcleo extraído): `potrace/inkscape-potrace.*`
  (brightness, quant, multi-scan), `potrace/bitmap.h`, `quantize.*` (octree),
  `filterset.*` (gaussiano, Canny), `imagemap*`, `pool.h`, `trace.*`
  (TracingEngine + SvgBuilder), `async/progress.h`.
- **Nuevo (namespace `ivf`)**: `imageio.*` (carga con alfa), `tracer.*`
  (fachada, `BackgroundPolicy`, preview honesta), `cli/main.cpp`, tests.
- **Eliminado en v1.1**: `src/core/3rdparty/autotrace/` (44 archivos),
  `trace/autotrace/`, `cielab.*`, `siox.*`, `util/safe-printf.h`, `src/gui/`.
- Parches al núcleo: solo los de alfa/progreso/bg-area, marcados
  `IVF PATCH` y listados en `EXTRACCION.md` §3.

## 4. Auditoría (objetivo 0)

Informe completo con severidades en `docs/AUDITORIA.md`. Hallazgos
principales, todos verificados por compilación/ejecución:

- **Confirmados**: composición de alfa sobre blanco (el "fantasma blanco");
  `pop_back()` ciego de remove-background; puntero de progreso de potrace
  colgante; `layer_area_fraction` medía geometría trazada (inútil en stack);
  autotrace mutaba el pixbuf de entrada (obsoleto tras eliminar autotrace).
- **Descartados tras verificación** (falsas sospechas del brief):
  `Glib::Quark("...")` es legal (construye desde const char*);
  `write_svg_path` de lib2geom acepta la firma de 3 argumentos;
  el hash de `Geom::Point` funciona vía el operador de lib2geom;
  el eje Y se maneja correctamente (`writePaths` usa la orientación del
  canvas, verificado con imagen asimétrica); `traceQuant`/`traceBrightnessMulti`
  reproducen fielmente el wiring upstream (comparados contra el repo).
- Limitación documentada (no bug): el modo mono con umbral 0.45 pierde un
  gris al 50 %; es el comportamiento binario esperado (ajustar `--threshold`).

## 5. Pruebas realizadas y resultados (v1.4)

- Suite automática: **ALL TESTS PASSED** (incluye: stack invertido — capa
  0 cubre el lienzo y capa N-1 no; paleta sin negros fantasma; imagen
  100 % transparente sin crash ni capas; alpha round-trip; políticas de
  fondo; previews consistentes).
- Verificación cuantitativa del cuadro blanco (render cairosvg vs
  original, distancia RGB media): buggy 240.6 → tile 37.8 →
  **stack corregido 11.6**.
- Verificación upstream: `traceQuant` 1.4.4 es idéntico byte a byte al
  extraído; el historial (39 commits de `src/trace`) no contiene fix de
  la acumulación ni del sort de paleta.

Suite automática (`tests/test_core.cpp`): **ALL TESTS PASSED**. Cobertura:

- Modo mono: círculo negro sobre blanco → 1 path, XML válido, xmlns+viewBox,
  capa negra.
- Modo color: círculo negro + cuadrado gris → capas separadas con colores
  correctos.
- **PNG con alfa** → sin capa blanca fantasma (`RemoveTransparent`); `KeepAll`
  respeta el opt-out.
- **Borde verde dominante** → capa verde eliminada, blanco interior conservado.
- **Umbral de área**: blanco en 1 % del centro NO se elimina con
  `remove-color`; blanco al 99 % SÍ.
- Preview mono consistente con el filtro del trazo; preview color con paleta.
- Round-trip de alfa en `imageio`.

Verificación CLI adicional (reproducciones manuales): píxel-negro-cuadrado
2 % con `remove-color '#ffffff'` conserva las dos capas; con `keep-all`
idéntico; la misma imagen con el cuadrado negro grande y fondo blanco
elimina solo el fondo.

## 6. Estado del build

- `cmake -B build && cmake --build build`: 0 errores.
- `ctest --test-dir build`: 100 % (1/1 suites, ~45 aserciones).
- Dependencias: glibmm-2.4, gdkmm-3.0, gdk-pixbuf, lib2geom, libxml2,
  potrace. Sin boost, sin gtkmm-widgets, sin autotrace.

## 7. Limitaciones y trabajo futuro

- **Depixelize** (pixel-art): no extraído (submódulo `libdepixelize`).
- Modo mono binario: grises cercanos al umbral pueden desaparecer (por
  diseño; usar `--mode color` o ajustar `--threshold`).
- Sin centerline (venía de autotrace, eliminado).
- Sin NLS; probado solo en Linux (Fedora). Windows/macOS: notas en
  `DEPENDENCIAS.md`, sin CI.
- ΔE CIE-Lab para el matching de fondo (mejor que la tolerancia RGB
  ±24/canal actual) — trabajo futuro priorizado.
- Cuantizador k-means opcional y "gradient step" estilo vtracer para
  gradientes — `docs/INVESTIGACION.md` §5.
- Reportar upstream (`stack-union`, `palette-sort`, `prune-nullcheck`)
  como issues/MRs en GitLab de Inkscape.

## 8. Cumplimiento GPL

- Licencia del proyecto: **GPL-2.0-or-later** (`LICENSE` = texto GPL-2.0
  completo, misma que Inkscape para los archivos extraídos).
- Todas las cabeceras `SPDX` y avisos de copyright de Inkscape/Potrace/
  lib2geom permanecen intactos.
- `NOTICE` atribuye Inkscape, Potrace (Peter Selinger), lib2geom, GTK/GLib,
  libxml2 (AutoTrace retirado en v1.1).
- `docs/COPYING.inkscape` incluido como referencia. Distribuir la app
  exige fuente u oferta de fuente, conservación de avisos y misma licencia;
  los enlaces dinámicos con lib2geom (LGPL-2.1+) y GTK/glib (LGPL-2.1+) son
  compatibles.
