# AUDITORÍA v2 — InkscapeVectorizerFull (v1.3)

Fecha: 2026-09-12 · Build: compila OK, ctest 1/1.
Método: reproducción real del bug con `examples/sample_input.png`, lectura
línea a línea, y **comparación directa contra upstream**
(`/tmp/inkscape`, tag INKSCAPE_1_4_4). Nada especulativo salvo lo marcado.

---

## 0.1 — BUG DEL CUADRO BLANCO (P0, confirmado)

**Reproducción** (`--scans 4 --background-policy keep-all`, sample_input.png):

| Modo | Paths | Colores |
|---|---|---|
| stack | 4 | `#030303`, `#7b4c4c`, `#814f4f`, `#f8f8f8` |
| tile | 4 | idénticos |

Dos evidencias problemáticas:

1. **`#7b4c4c` y `#814f4f` son casi el mismo color** → la paleta está
   corrupta (ver 0.2): con scans=4 la imagen debería dar 4 colores bien
   separados, no dos marrones gemelos.
2. **Stack mode**: el `GrayMap` se inicializa a WHITE **una sola vez**
   (`traceQuant`, línea ~382) fuera del bucle de colores. En stack mode
   `else if (!multiScanStack)` nunca ejecuta → sin reset. La capa i cubre
   la **unión** de los colores 0..i; la última (más clara) cubre todos los
   píxeles visibles y se emite al final (encima de todo) → **cuadro
   claro/blanco sólido tapando la imagen**. Es el bug reportado.

**Matiz importante verificado:** upstream Inkscape (líneas 377–420 de
`src/trace/potrace/inkscape-potrace.cpp`) tiene **exactamente el mismo
código, byte a byte** — misma acumulación, mismo orden de emisión (sin
invertir). Es decir, esto es un bug heredado de Inkscape, no introducido
por la extracción (posiblemente enmascarado upstream porque Inkscape
elimina la capa más clara por defecto con "remove background" activado,
y el `<g>` va debajo de la imagen original en el documento).

Los píxeles 100 % transparentes NO participan (IVF PATCH alpha-quant ya
los excluye), pero eso no salva el contenido visible: la capa más clara
lo cubre todo.

**Fix propuesto (Opción A del brief):** acumular al revés —
`if (index >= colorIndex)` — de modo que la capa 0 (oscura) cubra todo
(abajo) y la N-1 (clara) solo su propia región (arriba). Fallback
configurable `--stack-mode=union|hues` (Opción B: tile).

Severidad: **bloqueante**.

## 0.2 — `std::sort` sobre entradas-cero en `rgbMapQuantize` (P0, confirmado)

`quantize.cpp` líneas ~508–522:

```cpp
auto rgbs = std::make_unique<RGB[]>(ncolor);   // ceros
int index = 0;
octreeIndex(tree, rgbs.get(), index);          // rellena 0..index-1
std::sort(rgbs.get(), rgbs.get() + ncolor, ...); // ¡ordena las ncolor!
...
for (int i = 0; i < index; i++) imap.clut[i] = rgbs[i]; // copia las primeras `index`
```

Si la imagen real tiene 2 colores y ncolor=8: las 6 entradas restantes son
(0,0,0); el sort ascendente las sube al frente; el bucle copia las dos
primeras → **paleta corrupta** (dos negros), y los colores reales se
pierden. Además `findRGB(rgbs.get(), ncolor, ...)` (línea ~556) busca en
las `ncolor` entradas, incluyendo ceros → asigna negro a píxeles que no lo
son. **Explica los marrones gemelos `#7b4c4c/#814f4f`** de la reproducción.

Fix: `std::sort(rgbs.get(), rgbs.get() + index, ...)` y
`findRGB(rgbs.get(), index, ...)`. Upstream tiene el mismo defecto
(por confirmar en el log upstream; el código actual es idéntico).
Severidad: **bloqueante** (corrompe toda salida de cuantización con
paleta no llena).

## 0.3 — `octreePrune` nullptr-deref con imagen 100 % transparente (P0, confirmado)

`quantize.cpp` líneas 412–421 (idéntico upstream):

```cpp
int n = (*ref)->nleaf - ncolor;   // deref ANTES del chequeo
if (!*ref || n <= 0) return;
```

Con el parche alpha-quant, una imagen RGBA 100 % alpha=0 no crea hojas →
`octreeBuild` devuelve `nullptr` → SIGSEGV en `octreePrune`.
Fix: mover `if (!*ref) return;` antes de `(*ref)->nleaf`.
Severidad: **alta** (crash, caso borde).

## 0.4 — `load_image` rowstride: `scale_simple` no garantiza packed (P2, confirmado)

`imageio.cpp` líneas 26–30. `scale_simple` con las mismas dimensiones pasa
los píxeles por el escalador (no es un no-op garantizado y puede alterar
bordes). `Gdk::Pixbuf::copy()` produce un buffer nuevo compacto sin tocar
píxeles. Severidad: **media** (sutilmente incorrecta; en la práctica
gdk-pixbuf ya devuelve packed para la mayoría de formatos).

## 0.5 — Preview de color ignora alfa (P2, confirmado)

`indexedMapToGdkPixbuf` (imagemap-gdk.cpp líneas 109–129) crea pixbuf RGB
sin alfa y usa `getPixelValue` = `clut[getPixel % clut.size()]` — para
píxeles marcados `(unsigned)-1` (transparentes) devuelve `clut[255 % n]`.
Resultado: las zonas transparentes salen **negras** (o del color n) en la
preview de la GUI y en `--preview-out`. Fix: si `map.hasAlpha()`, crear
pixbuf RGBA y poner alpha=0 en esos píxeles (y el alfa real en los
semi-transparentes). Severidad: **media**.

## 0.6 — `--keep-background-rect` flag muerta (P3, confirmado)

Existe en `Settings`, CLI y README; `Tracer::trace` nunca la lee.
Decisión: **eliminarla** del CLI/README/Settings (la política `keep-all`
ya cubre el caso de conservar el fondo como geometría — que es lo que la
flag prometía de facto). Severidad: **baja**.

## 0.7 — Doble conteo de esquinas en `dominant_border_color` (P3, confirmado)

`tracer.cpp` ~línea 105: las 4 esquinas se suman dos veces a `count` y
solo se corrige `total`. Sesga hacia el color de esquina en imágenes
pequeñas. Fix: no contar esquinas dos veces (recorrer bordes con paso
que excluya esquinas duplicadas, o restarlas de `count`). Severidad: **baja**.

## 0.8 — Tolerancia de fondo ±8/canal demasiado estrecha (P1, confirmado)

`tracer.cpp`: `rgb_dist2(layer, bg) <= 3 * 8 * 8`. Con fondos
antialiased/JPEG (#fdfdfd, #f6f6f6) el match falla y el fondo sobrevive.
El brief propone ΔE CIE-Lab ≈ 10–15; sin dependencias nuevas, un
compromiso razonable es subir la tolerancia RGB a ~16–24/canal
(pesada por canal) y documentar la variante Lab como trabajo futuro.
Severidad: **alta** (afecta directamente al caso de uso "quitar fondo").

## 0.9 — Canales 1/2 (PNG GA/LA) → out-of-bounds (P1, confirmado por lectura)

`gdkPixbufToGrayMap`/`gdkPixbufToRgbMap` asumen nchannels 3/4 y leen
`p[2]`/`p[3]`. Con un PNG en escala de grises (1 canal o 2 con alfa,
que gdk-pixbuf entrega sin expandir en algunos loaders), se lee fuera
de rango. Fix barato y robusto: en `load_image`, si `nchannels < 3`,
convertir con `Gdk::pixbuf_add_alpha`-equivalente
(`pixbuf->add_alpha(false,0,0,0)`), que garantiza 4 canales. Severidad:
**alta** (lectura fuera de rango, aunque poco frecuente).

## 0.10 — Alfa parcial sigue pre-mezclado con blanco (P1, confirmado)

Pese al IVF PATCH (alpha), el RGB de los píxeles con alfa 1..254 sigue
compuesto sobre blanco en ambos convertidores:

```cpp
unsigned char r = (int)p[0] * alpha / 256 + white;  // white = 255 - alpha
```

(Nota adicional: la mezcla usa `/256` con `white=255-alpha`, lo que
además oscurece ligeramente incluso píxeles opacos: a=255 → r*255/256.)
Consecuencia: los bordes antialiased de un PNG con transparencia
inyectan tonos casi-blancos en el octree → ensucian la paleta (colores
fantasma claros) → luego no matchean el fondo. Fix: guardar el color
original sin pre-mezclar y usar `alpha` como **peso** en
`octreeBuildArea` (los pesos del octree ya son acumulables) y en el
promedio de `octreeIndex`. Severidad: **alta**.

## 0.11 — `<g>` innecesario en `getSVG` (P3, confirmado)

`SvgBuilder::getSVG` envuelve en `<g>` si >1 path. Cosmético; upstream
también lo hace (trace.cpp línea 500). **Decisión: no tocar** (paridad
con upstream, riesgo > beneficio).

## 0.12 — Preview mono duplicada (P3, confirmado)

`Tracer::preview` para Mono re-implementa `filter()` (brightness cutoff)
en vez de llamar `engine->preview(pixbuf)`, que para BRIGHTNESS ejecuta
el mismo `filter()`. Riesgo de desincronización (hoy coinciden: mismo
floor=0, misma fórmula `3*threshold*256`). Fix: llamar
`engine->preview()` también en mono. Severidad: **baja** (limpieza).

## 0.13 — Memoria/hilos en la GUI (P2, revisado)

- `TraceWindow` no tiene destructor que joinee el hilo si la ventana se
  cierra a mitad de trazo: `Glib::Thread` joinable destruido sin join →
  `g_thread_join` aborta el proceso al salir. Fix: cancelar + join en
  destructor (o al menos en `hide_event`).
- `joinWorker()` (ya correcto) se llama solo desde los dispatchers; si el
  usuario cierra antes del dispatcher, crash a la salida.
- `IVF PATCH (progress-lifetime)` verificado: el reset del callback existe
  y es correcto.
- Pixbufs intermedios: gestionados por RefPtr, sin leaks.

Severidad: **media** (crash solo al cerrar durante un trazo).

## 0.14 — `SvgBuilder` precisión y eje Y (sin hallazgos)

- `write_svg_path(item.path, 8, true)`: la firma existe en la lib2geom
  instalada (verificado en v1.0 y re-verificado ahora); 8 dígitos es más
  que suficiente (Inkscape usa precisión similar vía `sp_svg_write_path`).
- Eje Y: verificado en v1.1 con imagen asimétrica (L) y re-verificado:
  las coordenadas del SVG coinciden con la orientación de la imagen.
  Sin bug.

---

## Resumen de prioridades

| # | Bug | Severidad | Archivo |
|---|---|---|---|
| 0.1 | Cuadro blanco (stack acumulativo) | **Bloqueante** | inkscape-potrace.cpp |
| 0.2 | std::sort/findRGB sobre ceros | **Bloqueante** | quantize.cpp |
| 0.3 | octreePrune nullptr-deref | Alta | quantize.cpp |
| 0.8 | Tolerancia fondo ±8 | Alta | tracer.cpp |
| 0.9 | GA/LA out-of-bounds | Alta | imagemap-gdk.cpp/imageio.cpp |
| 0.10 | Alfa parcial pre-mezclado | Alta | imagemap-gdk.cpp, quantize.cpp |
| 0.4 | rowstride vía scale_simple | Media | imageio.cpp |
| 0.5 | Preview color sin alfa | Media | imagemap-gdk.cpp |
| 0.13 | GUI: join al cerrar | Media | gui/main.cpp |
| 0.6 | keep-background-rect muerta | Baja | tracer/cli/README |
| 0.7 | Doble conteo esquinas | Baja | tracer.cpp |
| 0.12 | Preview mono duplicada | Baja | tracer.cpp |
| 0.11 | `<g>` cosmético | — | decisión: no tocar (paridad upstream) |
| 0.14 | Precisión/Y | — | sin hallazgos |
