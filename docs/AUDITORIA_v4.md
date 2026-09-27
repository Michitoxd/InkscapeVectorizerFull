# AUDITORIA_v4 — Regresión v1.6: "motas y rayas" en zonas planas (modo color)

Fecha: 2026-09-12 · Versión afectada: 1.5.3/1.6 · Estado: **confirmado y corregido en 1.7**

## 0.1 Reproducción

Imagen de prueba `gato` (512², 4 colores únicos: blanco #ffffff de fondo,
marrón plano #8b5a2b, negro #141414, contorno #000000 — equivalente al
PNG del gato cartoon: zonas planas grandes + negro + blanco).

```bash
build/trace-bitmap --mode color --scans 8 gato.png out.svg
```

| Métrica (render cairosvg 512²) | v1.5.3/1.6 (smooth ON, defaults antiguos) | v1.7 (reverts + smooth OFF) |
|---|---|---|
| Capas (`<path>`) | 8 | 4 |
| Subpaths (`M`) | 30 | 10 |
| distRGB medio | 6.93 | **1.80** (−74 %) |
| Tamaño SVG | 15.3 KB | 3.9 KB |
| Tiempo | 0.12 s | 0.08 s |

Hallazgo clave de la auditoría: con blur ON, la paleta de 8 slots se
descompone en `#211d1a #4e361e #714b25 #8b5a2b #626262 #b08f70 #aba6a1
#fefefe` — **cinco slots de rampa que no existen en el original** (solo
hay 4 colores). El despeckle indexed absorbe parte de las islas de rampa,
pero deja el borde ragged = las "rayas" visibles. Con blur OFF la paleta
sale exacta (`#000000 #141414 #8b5a2b #ffffff`) y **0 px de speckle
interior** en la zona marrón (verificado: todo píxel "malo" está a ≤3 px
del borde antialiased del render, es AA del propio SVG, no speckle).

## 0.2 Causas confirmadas (con evidencia de código, no especulación)

### CAUSA A — `kmeans-refine` después de `palette-merge` · CONFIRMADA
`quantize.cpp` (v1.6): `rgbMapQuantize()` → sort por luminancia →
`palette-merge` (colapsa ≤2/canal) → `kmeansRefine()` **sin restricción
de separación**. Instrumentado con el harness `dbg2` sobre el gato:
el octree produce 4 colores reales; tras k-means+Lab la asignación
fragmenta. La corrección de Lloyd puede converger a centroides casi
idénticos (el merge previo se deshace), y con el umbral de empate
`d < dist` el píxel cae al índice más bajo → islas → motas.
**Fix: ELIMINADO** (`IVF PATCH (remove-kmeans)`), ver 1.1.

### CAUSA B — `lab-match` en `findRGB` · CONFIRMADA
`findRGB()` llamaba `distLab()` (conversión sRGB→Lab completa, con
`pow` y `cbrt`) **por píxel × por entrada de paleta, sin cachear**, y
truncaba a `int`. Dos efectos medidos: (a) amplifica diferencias
sub-ΔE entre centroides k-means vecinos — la zona plana se rompe;
(b) lentitud (≈ 3× el coste de asignación). Upstream usa `distRGB`.
**Fix: REVERTIDO** (`IVF PATCH (revert-lab-match)`), ver 1.2.

### CAUSA C — `removeSmallBlackIslands` por capa · CONFIRMADA (diseño roto)
En `traceQuant()`, la máscara de la capa k (stack-union: índices ≥ k)
puede contener una isla negra pequeña de índice k rodeada de índices
MAYORES (más claros). Al borrarla de la capa k, el píxel **no está
presente en ninguna otra máscara** (en las capas k' < k esos píxeles no
son BLACK si su índice real es k) → hueco sin pintar. El comentario de
v1.6 lo racionalizaba ("the area is still painted by lower layers") pero
eso solo es cierto cuando el vecino tiene índice MENOR; con vecinos de
índice mayor crea huecos blancos. **Fix: ELIMINADO**
(`IVF PATCH (remove-layer-despeckle)`), ver 1.3.

### CAUSA D — Umbral de despeckle fijo · CONFIRMADA
`max(12, 4*turdsize)` = 12 px con defaults: a 1024², un blob AA de 7×7
(49 px) sobrevivía. **Fix: umbral escalado** con `sqrt(w·h)/20`
(`IVF PATCH (despeckle-scale)`): 12 px @256², 25 @512², 51 @1024², ver 1.4.

### CAUSA E — `alpha-threshold = 128` · PARCIALMENTE CONFIRMADA, SE CONSERVA
Experimento controlado (frame AA con alfa 128 exacto): thr=128 y thr=1
producen el MISMO número de subpaths (4) en el repro del "cuadro".
El umbral 128 sigue siendo necesario: con `alpha == 0`, los píxeles
AA 1..127 de una región oscura contra transparencia entran al octree
como contenido casi negro y generan las motas del bug 1.5.2 (el fix
v1.5.2 de los puntos negros). Se documenta como decisión, no como
regresión: el costo teórico (corta contenido alfa 64..127) no se
manifestó en ningún caso de prueba.

### CAUSA F — Memoria de `kmeansRefine` · CONFIRMADA (moot tras 1.1)
`std::vector<int> counts(1 << 24)` = 64 MB + escaneo de 16.8M entradas
por llamada. Eliminado junto con k-means (CAUSA A).

## 0.3 Comparación con el Inkscape original (INKSCAPE_1_4_4)

| Aspecto | Inkscape upstream | Nuestro v1.6 | v1.7 |
|---|---|---|---|
| Asignación píxel→paleta | `distRGB` | `distLab` sin cachear | **`distRGB` (upstream)** |
| Refino k-means | No | Sí, tras el merge | **Eliminado** |
| Despeckle por capa | No | `removeSmallBlackIslands` | **Eliminado** |
| Despeckle indexed map | No | Sí (parche IVF) | Sí, umbral escalado |
| `multiScanSmooth` default (color) | OFF (checkbox) | ON | **OFF** |
| Blur 5×5 divisor 159 | Sí (mismo kernel) | Sí + bordes copiados | Sí + bordes reflejados |
| Sentinela de transparencia | `(unsigned)-1` con `==` | igual + `isTransparent()` α<128 | igual (thr 128 medido seguro) |

Conclusión: v1.6 se alejó del original con efectos medibles y negativos.
v1.7 restaura la semántica upstream en asignación/cuantización y conserva
únicamente los parches IVF con beneficio medido (stack-union,
palette-sort, palette-merge, alpha, despeckle indexed escalado).
