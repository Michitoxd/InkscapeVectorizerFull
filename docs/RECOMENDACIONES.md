# RECOMENDACIONES — lectura completa del código (v1.5)

Fecha: 2026-09-12 · Objetivo 4 del plan v1.5.

Cada sugerencia indica impacto / coste estimado / ¿rompe la simplicidad?

## 1. Bugs residuales y deuda técnica

| # | Ítem | Archivo | Impacto | Coste | ¿Simplifica? |
|---|---|---|---|---|---|
| R1 | `grayMapGaussian` copia el borde sin filtrar (franja de 2 px sin suavizar en imágenes pequeñas). Ya propaga alfa; falta manejo de borde reflexivo. | `filterset.cpp` | Bajo | ~20 líneas | No |
| R2 | `dominant_border_color` usa distancia RGB euclídea; colores cercanos (#fdfdfd vs #ffffff) pueden no agruparse. Migrar a CIE-Lab ΔE≈10. | `tracer.cpp` | Medio | ~60 líneas + lookup Lab | No |
| R3 | `Settings::remove_bg_color` no distingue "no fijado" de `#ffffff`; con `remove-border` + `remove-color` simultáneos la semántica es ambigua. Añadir `std::optional<uint32_t>`. | `tracer.h` | Bajo | ~10 líneas | Sí (menos estados ocultos) |
| R4 | La preview de color dibuja índices transparentes como huecos (correcto), pero la GUI aún compone su preview sobre fondo del tema del widget. Verificar con PNG alfa desde la GUI. | `gui/main.cpp` | Bajo | revisión | — |
| R5 | `write_path_for_check` en tests duplica parseo de `d=""` frágil. Extraer helper común si crece la suite. | `tests/test_core.cpp` | Bajo | ~30 líneas | No |

## 2. Arquitectura (manteniendo simplicidad)

- **A1 — El centinela `(unsigned)-1` ya no debería existir.** Con
  `IndexedMap::isTransparent()` (v1.5) el centinela es un detalle de
  implementación de `rgbMapQuantize`. Paso futuro: hacer que el octree
  devuelva un `IndexedMap` con un campo `std::vector<bool> visible`
  paralelo, y borrar el centinela del todo. Impacto alto (elimina una
  clase entera de bugs); coste medio; **simplifica**.
- **A2 — `Tracer::make_engine()` está bien separado**; la fachada no
  filtra detalles del motor. No tocar.
- **A3 — Dos ramas de filtro (mono `filter()` / color `filterIndexed()`)**
  son razonables; no unificarlas (potrace binario vs indexed tienen
  flujos distintos).
- **A4 — BackgroundPolicy es extensible pero cerrada**: si se añade
  CIE-Lab (R2), implementarlo dentro de `Tracer::trace` como función
  libre, no como nueva política.
- **A5 — Settings no usados ya: ninguno** (la flag muerta
  `--keep-background-rect` se eliminó en v1.5). ✅

## 3. Funcional

| # | Sugerencia | ¿Vale la pena? | Impacto | Coste | Riesgo |
|---|---|---|---|---|---|
| F1 | Cuantizador k-means opcional (`--quantizer kmeans\|octree`) | Sí, para fotos: k-means minimiza el error real, el octree no | Alto | ~150 líneas | Bajo (flag opt-in) |
| F2 | Matching de fondo CIE-Lab ΔE (R2) | Sí | Medio | ~60 líneas | Bajo |
| F3 | Deduplicación de writePaths por huella de subpath (ya hecha en v1.5 como `write-paths-dedup`) — validar con casos reales de texto | Hecha; seguir monitorizando | — | — | — |
| F4 | Detección de borradores upstream de Inkscape en `src/trace/` (diff trimestral contra el tag) | Sí como proceso | Medio | 0 código | — |
| F5 | Modo pixel-art (nearest, sin gauss, threshold por bloque) | Tal vez; es un preset + `nearest` en carga | Medio | ~80 líneas | Bajo |
| F6 | Post-procesado de curvas con lib2geom (simplificación adicional) | No por ahora: `opt-tolerance` ya lo cubre; medir primero | Bajo | ? | Medio |

## 4. Ergonomía

- **E1 — Presets ya presentes** (`line-art|logo|photo|high`). Falta
  exponerlos en la GUI como combo box (hoy la GUI tiene los parámetros
  sueltos). Impacto medio; ~40 líneas; no simplifica pero ayuda mucho.
- **E2 — CLI**: el flujo "preset primero, flags después sobrescriben"
  está documentado en `--help`. Sugerencia: ejemplo de preset en la
  primera línea de `--help`.
- **E3 — README**: ya dice explícitamente que Potrace pierde gradientes
  (docs/FIDELIDAD.md tiene la medición). Mantener esa honestidad.
- **E4 — Defaults actuales**: `scans 8` y `smooth on` son buenos para
  logos (el caso más común); para fotos el usuario debe usar `--preset
  photo`. Considerar auto-selección por heurística (nº de colores
  únicos) en v2 — impacto alto, coste medio, riesgo de "magia" — dejar
  para después de F1.

## 5. Mantenimiento

- **M1 — Actualizar el tag de Inkscape**: repetir el proceso de
  `docs/EXTRACCION.md` §2 con el nuevo tag, y re-aplicar los parches en
  orden documentado (`alpha`, `alpha-quant`, `alpha-nopremult`,
  `stack-union`, `alpha-sentinel-guard`, `mono-smooth`,
  `write-paths-dedup`, `gaussian-alpha`, `bg-area`, `palette-sort`,
  `prune-nullcheck`, `progress-lifetime`, `ga-la-expand`,
  `packed-copy`). Cada parche es pequeño y confinado; documentar
  cualquier conflicto.
- **M2 — Reportar a upstream**: los fixes `stack-union` (cuadro
  blanco), `alpha-sentinel-guard` (fondo negro) y `palette-sort`
  afectan a Inkscape también (verificado: upstream es byte a byte
  igual). Los MRs a `gitlab.com/inkscape/inkscape` con los tests de
  `tests/test_core.cpp` como evidencia son el mejor vehículo.
- **M3 — Tests**: añadir caso de texto real (fuente con muchos
  contornos que comparten esquinas) para vigilar `write-paths-dedup`,
  y un test de la GUI bajo Xvfb en CI (hoy solo se probó manualmente).

## 6. Qué NO hacer

- No añadir más modos públicos: mono + color + presets cubren el 95 %
  de casos; el resto es configuración.
- No reemplazar el octree sin opción explícita (rompería reproducibilidad).
- No crear una capa de abstracción sobre `PotraceTracingEngine`: con un
  solo motor, la indirección es deuda, no diseño.
