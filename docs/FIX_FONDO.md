# FIX_FONDO — el bug histórico de fondo blanco y transparencia

Fecha: 2026-09-12 · Versión: 1.2

## 1. El bug original (heredado de Inkscape)

Inkscape (y esta extracción, hasta 1.1) tenía dos problemas relacionados con
el fondo de las imágenes vectorizadas:

### 1.1 Transparencia compuesta sobre blanco

En `imagemap-gdk.cpp`, los convertidores `gdkPixbufToGrayMap` y
`gdkPixbufToRgbMap` mezclaban el canal alfa sobre blanco:

```c
// código original de Inkscape
r = r * a / 256 + (255 - a);
```

Toda zona transparente se convertía en blanco puro *antes* de trazar. El
cuantizador octree creaba entonces una capa `#ffffff` (o casi blanca) que
cubría todo el lienzo, y el SVG final tenía un fondo blanco opaco donde
antes había transparencia. Es el clásico "fantasma blanco" al vectorizar
PNG con alfa.

### 1.2 "Remove background" a ciegas

En `inkscape-potrace.cpp` (`traceQuant` y `traceBrightnessMulti`):

```c
if (results.size() > 1 && multiScanRemoveBackground) {
    results.pop_back();   // elimina la ÚLTIMA capa
}
```

Como las capas multi-scan se generan de más oscura a más clara, la última
suele ser la más clara. Pero si la imagen tiene contenido blanco legítimo
(ojos, dientes, reflejos, texto blanco), esas regiones se borraban junto
con el fondo: el usuario veía "me borra partes de mi imagen".

## 2. La solución (v1.2)

### 2.1 El alfa se preserva (IVF PATCH (alpha))

- `GrayMap` y `RgbMap` llevan ahora un vector `alpha` paralelo
  (`imagemap.h`). Vacío = imagen opaca.
- `gdkPixbufToGrayMap` / `gdkPixbufToRgbMap` copian el alfa tal cual, sin
  componer sobre blanco.
- `rgbMapQuantize` (IVF PATCH (alpha-quant)): los píxeles con alfa == 0 no
  contribuyen al histograma del octree y se marcan como "sin color" en el
  mapa indexado; no pertenecen a ninguna capa. Los píxeles con alfa
  intermedio contribuyen ponderados por su alfa.

### 2.2 Política de fondo explícita (`BackgroundPolicy`)

Nada de `pop_back()` a ciegas. La fachada `ivf::Tracer` decide qué capas
emite según `--background-policy`:

| Política | CLI | Qué hace |
|---|---|---|
| `KeepAll` | `keep-all` | No elimina nada. |
| `RemoveTransparent` | `remove-transparent` | Si la imagen tenía alfa, las zonas transparentes simplemente no llegan a ninguna capa: quedan como huecos en el SVG. Si la imagen no tenía alfa, degrada a `RemoveDominantBorderColor`. |
| `RemoveDominantBorderColor` | `remove-border` | Detecta el color dominante del borde (≥60 % de los píxeles del borde). Elimina SOLO la capa cuyo color de paleta coincide con ese color. |
| `RemoveDetectedByColor` | `remove-color` | Elimina la capa más cercana a `--remove-bg-color '#RRGGBB'`, pero solo si ese color cubre ≥ `--background-area-threshold` (0.95 por defecto) de los píxeles visibles. |

**Default (`auto`):** `RemoveTransparent` (con degradación a
`RemoveDominantBorderColor` para imágenes opacas).

### 2.3 El umbral de área protege el contenido legítimo (IVF PATCH (bg-area))

Importante: el área se mide sobre los **píxeles reales de la imagen fuente**
con ese color, no sobre la geometría trazada de la capa (en modo stack la
geometría de cada capa cubre ~100 % del lienzo por construcción, lo que
hacía inútil cualquier medición geométrica).

- Con `remove-color`: si el blanco solo ocupa el 1 % (un ojo, un reflejo),
  1 % < 95 % → NO se elimina nada.
- Con `remove-border`: la evidencia es la dominancia del borde; el color
  detectado se elimina aunque tenga bolsillos interiores (esos bolsillos
  quedan como huecos y muestran el fondo de la página, que es lo esperado
  al quitar un fondo).

Tolerancia de color: ±8 por canal (RGB). Configurable en código
(`bg_color_area_fraction(pixbuf, bg, 8)` y `rgb_dist2(...) <= 3 * 8 * 8`).

### 2.4 Salida SVG

- Ninguna política emite un `<rect>` blanco de fondo.
- Con `RemoveTransparent`/`RemoveDominantBorderColor`, las zonas de fondo
  no tienen geometría: el SVG es transparente donde la imagen lo era.
- `--keep-background-rect` no es necesario: usar `--background-policy keep-all`
  para conservar todas las capas.

## 3. Antes / después

Entrada: PNG RGBA 64×64, disco negro sobre fondo transparente.

**v1.1 (bug):** el SVG contenía además una capa casi blanca
(`#fdfdfd`) que cubría todo el lienzo — la transparencia convertida en
fondo opaco.

**v1.2:** el SVG solo contiene la capa negra del disco; el resto del lienzo
queda transparente:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <path style="fill:#000000" d="..."/>
</svg>
```

## 4. Cobertura por tests (`tests/test_core.cpp`)

- PNG con alfa → no se genera capa blanca fantasma (`RemoveTransparent`).
- `KeepAll` sigue emitiendo todas las capas (opt-out del usuario).
- Imagen opaca con borde verde y contenido blanco interior → el verde se
  elimina, el blanco sobrevive.
- `remove-color` con blanco solo en el centro (1 % del área) → NO se elimina.
- `remove-color` con blanco en el 99 % del área → SÍ se elimina.
- Round-trip de alfa en `imageio`.
- (v1.5) Guard del centinela: PNG RGBA con disco negro sobre fondo
  100% transparente (RGB=0,0,0, alpha=0) → ningún path cubre el
  viewBox completo.

## 5. La regresión del fondo negro (v1.4 → v1.5)

**Síntoma (v1.4):** "antes el PNG se volvía blanco, ahora se vuelve
NEGRO" en modo color con PNGs transparentes.

**Mecanismo — interacción de tres parches:**

1. `alpha-nopremult` (v1.4): los conversores guardan el RGB crudo del
   PNG. En la mayoría de PNGs transparentes, el RGB subyacente de las
   zonas con alpha=0 es (0,0,0).
2. `alpha-quant` (v1.2): `rgbMapQuantize` marca los píxeles
   transparentes con el centinela `(unsigned)-1` y no contribuyen a la
   paleta.
3. `stack-union` (v1.4, fix del cuadro blanco): `traceQuant` pasó de
   `index == colorIndex` a `index >= colorIndex`. El centinela
   (4294967295) es `>=` que cualquier `colorIndex` → los píxeles
   transparentes se pintaban BLACK en TODAS las capas → la capa base
   oscura (que cubre todo el lienzo) incluía el fondo transparente →
   **rectángulo negro opaco**.

El comentario de `quantize.cpp` ("values >= nrColors are ignored there
because they can never match a colorIndex loop below") documentaba el
invariante del `==` original; `stack-union` lo rompió silenciosamente.
Lección: un centinela numérico solo es seguro si TODAS las comparaciones
posibles lo tratan explícitamente.

**Fix (v1.5) — `IVF PATCH (alpha-sentinel-guard)`:**

- En `traceQuant`, el centinela se comprueba con `==` ANTES de la
  comparación de stack/tile y se pinta WHITE (no contribuye a ninguna
  capa; las zonas transparentes quedan sin geometría en el SVG).
- `IVF PATCH (gaussian-alpha)`: el gaussiano propaga el alfa, para que
  los píxeles semitransparentes del borde no absorban colores vecinos.
- `IndexedMap::isTransparent(x, y)`: consulta el alfa real en vez de
  confiar en el centinela (base para eliminarlo por completo, ver
  docs/RECOMENDACIONES.md A1).

**Verificación:** test "alpha sentinel guard" en `tests/test_core.cpp`
(repro exacto del usuario) + medición de render: centro negro opaco,
esquinas sin geometría, ~4963 px pintados = disco real.

