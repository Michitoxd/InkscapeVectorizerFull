# InkscapeVectorizerFull

Vectorizador independiente de imágenes bitmap a SVG, construido sobre el
núcleo de "Trace Bitmap" extraído de Inkscape (tag `INKSCAPE_1_4_4`,
commit `dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871`) — **con los bugs
heredados de Inkscape corregidos**: el "cuadro blanco" del modo color
stack (capas mal ordenadas, `docs/FIX_CUADRO_BLANCO.md`), la paleta
corrupta de la cuantización octree, el borrado ciego de fondos
(`docs/FIX_FONDO.md`) y el fondo negro en PNGs transparentes
(regresión 1.4 corregida en 1.5, `docs/FIX_FONDO.md` §5).

Solo dos modos, sin dependencia de Inkscape. Disponible como **CLI** y como
**GUI GTK3** con preview en vivo.

## GUI

```bash
build/trace-bitmap-gui
# o, tras instalar:
trace-bitmap-gui
```

Ventana con: abrir imagen, modo mono/color, todos los parámetros del CLI,
preview en vivo (usa exactamente el mismo filtro que el trazo — nunca miente),
trazo en hilo de fondo con progreso y cancelación, y Exportar SVG.

La GUI es opcional en tiempo de compilación:
`cmake -B build -DIVF_BUILD_GUI=OFF` la omite (no requiere gtkmm-3.0).

## Modos

### `mono` — monocromo (binario)

Brightness cutoff de Potrace con suavizado gaussiano y optimización de
curvas (el equivalente al "Brightness cutoff" de Inkscape con Smooth y
Optimize activados). Un solo color (negro por defecto) sobre fondo
transparente.

```bash
build/trace-bitmap --mode mono logo.png logo.svg
build/trace-bitmap --mode mono --threshold 0.35 --invert foto_oscura.svg
```

### `color` — color real (multi-scan por cuantización)

Cuantización octree de Inkscape (`rgbMapQuantize`) + `traceQuant`:
N capas apiladas de más oscura a más clara (equivalente a "Multiple
scans: color" de Inkscape).

```bash
build/trace-bitmap --mode color --scans 8 imagen.png imagen.svg
build/trace-bitmap --mode color --scans 16 --no-stack --tile arte.png arte.svg
```

## CLI

```
trace-bitmap [opciones] <input> <output.svg>

  --preset NAME              line-art | logo | photo | high  (ver abajo)
  --mode mono|color          Modo de vectorización (default: mono)
  --scans N                  Número de colores en modo color (2..256, default 16)
  --smooth / --no-smooth     Gaussiano antes de cuantizar/binarizar.
                             Mono: on por defecto. Color: OFF por defecto
                             (como el checkbox "Smooth" de Inkscape en
                             multicolor; actívalo solo para line-art difuso)
  --stack / --tile           Apilar capas (default) o superponer por pasada
  --threshold N              Umbral de brillo en modo mono (0..1, default 0.45)
  --optimize / --no-optimize Optimizar curvas (default: on)
  --opt-tolerance N          Tolerancia de optimización (default 0.2)
  --smooth-corners N         Suavizado de esquinas (0..1.334, default 1.0)
  --speckles N               Tamaño mínimo de mota en píxeles (default 2).
                             Sube este valor (4-8) si tu imagen vectorizada
                             sigue teniendo puntos/motas espurios; 0 desactiva
                             el despeckle y conserva las motas
  --invert                   Invertir antes de vectorizar
  --background-policy P      keep-all | remove-transparent | remove-border |
                             remove-color | auto (default: auto)
  --remove-bg-color '#RRGGBB'  Color para la política remove-color
  --background-area-threshold F  Fracción de área para remove-color (default 0.95)
  --preview-out preview.png  PNG con la preview del filtro (sin Potrace)
  --quiet                    Sin salida en progreso
  --help                     Ayuda
```

### Presets de calidad (v1.5)

Un preset aplica un paquete de ajustes; cualquier flag posterior a
`--preset` lo sobrescribe. Medición cuantitativa en `docs/FIDELIDAD.md`
y `docs/FIDELIDAD_v2.md` (v1.7).

### Defaults de color (v1.7) y por qué

- `--scans 16` (antes 8): más fidelidad de color en el caso típico.
- `--no-smooth` implícito en color: el gaussiano 5×5 convierte cada
  borde duro en una rampa de color que consume slots de paleta y deja
  motas/rayas en zonas planas (medido: distRGB 6.93 → 1.80 en el gato
  de prueba; docs/AUDITORIA_v4.md). En mono sigue activado porque ahí
  la rampa cae a un único umbral de binarización.
- El despeckle del mapa indexado escala con la resolución
  (`√(w·h)/20`), así que las motas de AA no reaparecen en imágenes
  grandes.

```bash
# line-art / texto: contornos limpios sin trazos espurios
trace-bitmap --preset line-art dibujo.png dibujo.svg

# logos a color: pocas capas, esquinas afiladas
trace-bitmap --preset logo logo.png logo.svg

# fotografía: 32 capas, sin suavizado, conserva detalle
trace-bitmap --preset photo foto.jpg foto.svg

# máxima fidelidad (48 capas; SVG grande)
trace-bitmap --preset high foto.jpg foto.svg

# preset + override individual
trace-bitmap --preset photo --scans 48 foto.jpg foto.svg
```

Nota honesta: el motor es Potrace (binario por capa). Los gradientes se
aproximan por bandas y el antialiasing no se reproduce — es la misma
limitación de Inkscape. Los presets son la vía para maximizar fidelidad.

### Fondo y transparencia (bugfix v1.2, fondo negro corregido en v1.5)

Los PNG con alfa ya **no** generan un fondo blanco fantasma, y el fondo se
elimina por color detectado, no "a ciegas". Ver `docs/FIX_FONDO.md`.

```bash
# auto (default): transparente si hay alfa; si no, por color del borde
build/trace-bitmap --mode color imagen.png out.svg

# eliminar un fondo concreto solo si cubre >= 95% de la imagen
build/trace-bitmap --mode color --background-policy remove-color \
    --remove-bg-color '#ffffff' foto.png out.svg

# conservar todas las capas
build/trace-bitmap --mode color --background-policy keep-all foto.png out.svg
```

## Compilación

```bash
./scripts/build.sh
# o manualmente:
cmake -B build && cmake --build build
```

Dependencias: glibmm-2.4, gdk-pixbuf (y gdkmm-3.0), lib2geom, libxml2,
potrace (+ gtkmm-3.0 solo para la GUI; se puede desactivar con
`-DIVF_BUILD_GUI=OFF`). Ver `docs/DEPENDENCIAS.md`.

## Tests

```bash
ctest --test-dir build
# o directamente:
./build/tests/test_core
```

## Estructura

```
src/core/trace/          núcleo extraído de Inkscape (potrace, octree, filtros)
src/core/                fachada ivf (Tracer, BackgroundPolicy, imageio)
src/cli/                 trace-bitmap (CLI)
src/gui/                 trace-bitmap-gui (GTK3)
tests/                   test_core
docs/                    auditoría, arquitectura, extracción, dependencias, fix_fondo
```

## Licencia

GPL-2.0-or-later (la misma que Inkscape). Ver `LICENSE`, `NOTICE` y
`docs/EXTRACCION.md` para las atribuciones y la lista de parches
(`IVF PATCH`).
