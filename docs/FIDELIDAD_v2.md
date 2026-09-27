# FIDELIDAD_v2 — Tabla cuantitativa antes/después (v1.7)

Fecha: 2026-09-12 · Render de referencia: cairosvg al tamaño del original.
Métrica distRGB = suma |ΔR|+|ΔG|+|ΔB| medio por píxel (menor = mejor).

## 1. Gato sintético (512², 4 colores planos: blanco/marrón/negro)

| Config | Capas | Subpaths | distRGB | KB | Tiempo |
|---|---|---|---|---|---|
| v1.5.3/1.6-equivalente (smooth ON, kmeans+Lab, thr 12) | 8 | 30 | 6.93 | 15.3 | 0.12 s |
| **v1.7 defaults (smooth OFF, reverts, thr escalado)** | 4 | 10 | **1.80** | **3.9** | **0.08 s** |
| v1.7 con scans=16 | 4 | 10 | 1.80 | 3.9 | 0.08 s |

Speckle interior en la zona marrón plana (píxeles erróneos a >6 px de
cualquier borde de rasgo): v1.6 = islas de rampa visibles ("rayas");
v1.7 = **0 px**.

## 2. examples/sample_input.png

| Config | Capas | Subpaths | distRGB | KB | Tiempo |
|---|---|---|---|---|---|
| v1.5.3-equivalente (scans 8, smooth ON) | 7 | 44 | 16.89 | 50.6 | 0.13 s |
| **v1.7 defaults (scans 16, smooth OFF)** | 13 | 120 | **11.10** | 167.6 | 0.14 s |

Nota: scans 16 sube el número de nodos (más capas = más detalle) pero
baja la distancia de color un 34 %.

## 3. Benchmark de tiempo (imágenes planas sintéticas)

| Tamaño | scans | v1.7 (sin kmeans/Lab) |
|---|---|---|
| 256×256 | 8 | 0.06 s |
| 512×512 | 16 | 0.08 s |
| 1024×1024 | 32 | 0.18 s |

La eliminación de k-means quita por llamada: alloc de 64 MB
(`counts(1<<24)`), barrido de 16.8M cubetas y 4 iteraciones de
asignación Lab sobre el histograma (la parte más cara: `pow`+`cbrt`
por color × palette). La eliminación de `lab-match` quita la conversión
Lab por píxel×paleta en `findRGB` (O(w·h·colors) con `pow`/`cbrt`).

## 4. Umbral de despeckle escalado (despeckle-scale)

| Imagen | thr viejo `max(12, 4·turd)` | thr nuevo `max(viejo, √(w·h)/20)` |
|---|---|---|
| 256² | 12 px | 12 px (igual) |
| 512² | 12 px | **25 px** |
| 1024² | 12 px | **51 px** |

## 5. Regresiones veridicas cubiertas por tests (tests/test_core.cpp)

- `flat-zone`: parche uniforme 100×100 → **1 subpath** exacto en su capa.
- `near-identical`: dos mitades #7b4c4c / #7d4e4d → ≤6 subpaths totales
  (v1.6 generaba fragmentación).
- `mono-alpha`, `alpha-sentinel`, `mono-despeckle`, `index-despeckle`:
  los fixes de 1.5.x siguen cubiertos (79 checks en total, todos PASS).
