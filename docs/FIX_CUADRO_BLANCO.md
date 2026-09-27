# FIX_CUADRO_BLANCO — el bug del cuadro blanco en modo color

Fecha: 2026-09-12 · Versión: 1.4 · Severidad original: bloqueante

## 1. El bug

"Cuando vectorizo una imagen a color, todas las opciones me dan un cuadro
blanco sólido."

## 2. Mecanismo (causa raíz)

En `traceQuant` (`src/core/trace/potrace/inkscape-potrace.cpp`), el
GrayMap de trabajo se inicializa a WHITE **una sola vez, fuera del bucle
de colores**:

```c
auto gm = GrayMap(imap.width, imap.height);
// ... todo a WHITE ...

for (int colorIndex = 0; colorIndex < imap.nrColors; colorIndex++) {
    for (row, col) {
        int index = imap.getPixel(col, row);
        if (index == colorIndex) {
            gm.setPixel(col, row, GrayMap::BLACK);  // pinta a BLACK
        } else if (!multiScanStack) {
            gm.setPixel(col, row, GrayMap::WHITE);  // reset SOLO en tile
        }
    }
    auto pv = grayMapToPath(gm, ...);   // traza la ACUMULACIÓN
    results.emplace_back(style, pv);    // capa i
}
```

En stack mode (el default), `else if (!multiScanStack)` nunca se ejecuta:
no hay reset. Consecuencias:

- La capa i cubre la **unión** de los colores 0..i.
- La última capa (colorIndex = N-1, el color más claro) cubre **todos**
  los píxeles visibles.
- Las capas se emiten en orden → la última queda **encima de todo**.
- Resultado: un cuadrado macizo claro/blanco tapando la imagen.
  Exactamente lo que veía el usuario.

Agravante: la paleta llegaba además corrupta por el bug `palette-sort`
(§3), lo que producía capas duplicadas casi idénticas (`#7b4c4c` /
`#814f4f`) y hacía el resultado aún más incomprensible.

**Este código es idéntico byte a byte al de Inkscape 1.4.4** (verificado
contra el repo upstream): es un bug heredado de 2006
(commit `dd7634180`, "refactoring, add background removal") que upstream
nunca corrigió — en Inkscape pasa más desapercibido porque el diálogo
activa "remove background" por defecto y la capa clara se descarta con el
`pop_back()` ciego (que a su vez borra contenido blanco legítimo: ver
`docs/FIX_FONDO.md`).

## 3. Los dos fixes (v1.4)

### 3.1 `IVF PATCH (palette-sort)` — paleta no corrupta (P0)

`quantize.cpp` ordenaba las `ncolor` entradas del array aunque
`octreeIndex` solo rellenara `index` (las demás quedan a cero). Los ceros
subían al frente y se copiaban a la paleta como negros fantasma, y
`findRGB` asignaba negro a píxeles arbitrarios.

Fix: `std::sort(rgbs.get(), rgbs.get() + index, ...)` y
`findRGB(rgbs.get(), palette_size, ...)` — solo entradas válidas.

### 3.2 `IVF PATCH (stack-union)` — acumulación invertida (P0, Opción A)

Cambia la condición de pintado para que la capa i cubra los colores
**i..N-1** en vez de 0..i:

```c
if (multiScanStack ? (index >= colorIndex) : (index == colorIndex))
    gm.setPixel(col, row, GrayMap::BLACK);
else
    gm.setPixel(col, row, GrayMap::WHITE);
```

Efecto:

- Capa 0 (más oscura): cubre TODO el lienzo → se dibuja primero (abajo).
- Capa 1: cubre colores 1..N-1 (huecos donde estaba el 0).
- Capa N-1 (más clara): solo su propia región → dibujada al final (arriba).
- Cada píxel acaba mostrando su propio color. Sin cuadro blanco.

Coste documentado: los shapes de las capas oscuras son grandes (todo el
lienzo menos huecos) → más nodos y SVG algo más pesado que tile. El
modo `tile` sigue disponible y es más ligero.

## 4. Verificación (medida, no a ojo)

Render del SVG con cairosvg vs imagen original, distancia RGB media
(0 = idéntico, 441 = máximo):

| Salida | Dist. RGB media |
|---|---|
| stack ANTES del fix (el "cuadro blanco") | **240.6** |
| tile | 37.8 |
| stack DESPUÉS del fix | **11.6** |

El stack corregido es incluso más fiel que tile (los apilados cierran
rendijas antialiased). Estructura verificada de `out_fixed.svg`:

- Capa 0 (`fill:#030202`): 1 subpath — rectángulo completo
  `M0 256V0H256 512V256 512H256 0z` (la base oscura).
- Capas 1..N-1: anillos con huecos que dejan ver la capa inferior.

Tests añadidos (`tests/test_core.cpp`): capa 0 cubre el lienzo completo;
la capa N-1 ya no lo cubre; la paleta sobrecuantizada colapsa a los
colores reales; imagen 100 % transparente no crashea ni emite capas.

## 5. Uso

```bash
# stack correcto (default): base oscura abajo, clara arriba
build/trace-bitmap --mode color --scans 4 imagen.png out.svg

# tile (más ligero, sin solapamientos)
build/trace-bitmap --mode color --scans 4 --tile imagen.png out.svg
```

## 6. Relación con FIX_FONDO

El "remove background" ciego de Inkscape (`pop_back`) era el parche
empírico que escondía este bug: al borrar siempre la última capa (la del
cuadro claro), el resultado *parecía* correcto… a costa de borrar todo el
contenido claro legítimo. Con `stack-union` + `BackgroundPolicy`, el fondo
se elimina por evidencia (borde dominante o color explícito con umbral de
área) y el contenido legítimo sobrevive. Ver `docs/FIX_FONDO.md`.

## 7. Regresión introducida y corregida (v1.5) — nota cruzada

`stack-union` cambió la condición a `index >= colorIndex`, pero el
parche `alpha-quant` marca los píxeles transparentes con el centinela
`(unsigned)-1`. Con `>=`, el centinela pasaba el test para todo
`colorIndex` → los píxeles transparentes se pintaban BLACK en todas las
capas → **fondo negro opaco** en PNGs con transparencia (RGB negro
típico). El comentario de `quantize.cpp` ("values >= nrColors are
ignored there") era cierto para el `==` original pero falso con `>=`.

Arreglado en v1.5 con `IVF PATCH (alpha-sentinel-guard)`: el centinela
se comprueba con `==` antes de cualquier otra comparación y no
contribuye a ninguna capa. Detalle completo en `docs/FIX_FONDO.md` §5
y `docs/AUDITORIA_v3.md` §0.1.
