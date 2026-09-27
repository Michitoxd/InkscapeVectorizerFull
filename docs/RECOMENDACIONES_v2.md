# RECOMENDACIONES_v2 — lectura completa del código tras la regresión v1.6

Fecha: 2026-09-12 · Sustituye a `RECOMENDACIONES.md` (sus R1/R2/R4 ya
están implementados: gaussian-border, bg-lab, preview-checkerboard).

## 4.1 Bugs residuales y deuda técnica (estado tras v1.7)

| # | Ítem | Estado | Severidad |
|---|---|---|---|
| 1 | `findRGB` sin cachear Lab | **Resuelto** (revertido a distRGB) | — |
| 2 | `kmeansRefine` 64 MB + 16.8M iteraciones | **Resuelto** (eliminado) | — |
| 3 | `removeSmallBlackIslands` crea huecos | **Resuelto** (eliminado) | — |
| 4 | Umbral despeckle no escalaba | **Resuelto** (despeckle-scale) | — |
| 5 | `bg_color_area_fraction` escanea la imagen completa por capa coincidente | Deuda menor; se ejecuta 1× por traza, no por capa | Baja |
| 6 | `imapDespeckle` pasa 2–3 con `nbh[256]` fijo: paletas >256 imposible (ncolor ≤256, ok) pero `nv % 256` silenciaría colisiones si el límite cambia | Frágil si se sube el límite de scans | Baja |
| 7 | `write_path_for_check` en tests duplica parseo de `d=""` | Deuda conocida (R5 de v1.5) | Baja |
| 8 | GUI no expone presets (E1 de v1.5) | Pendiente | Media (UX) |

## 4.2 Mejoras reales que quedan (prioridad impacto/coste/riesgo)

1. **Cuantizador k-means BIEN hecho, opt-in** (`--quantizer octree|kmeans`):
   histograma con `unordered_map`, Lab cacheado por color de la paleta y
   del histograma (no por píxel), separación mínima post-Lloyd (fusionar
   centroides a <ΔE 4 tras cada iteración), paleta re-sorteada al final.
   Así se recupera la IDEA de v1.6 sin su regresión. ~150 líneas.
2. **Auto-scans**: si el nº de colores únicos (cuantizado a 5 bits/canal)
   supera scans·4, sugerir (o aplicar en modo `--preset auto`) scans=24.
   Cero riesgo si es solo sugerencia.
3. **Pixel-art preset**: `nearest` en carga, smooth OFF, speckles OFF,
   alphamax 0.5. Solo preset + flag de resampling; ~20 líneas.
4. **Post-curvas con lib2geom**: solo si FIDELIDAD muestra nodos
   redundantes; hoy `opt-tolerance` lo cubre.

## 4.3 Qué NO hacer (lecciones de la regresión v1.6)

- NO introducir heurísticas que se alejen de Inkscape sin medición
  antes/después sobre ≥3 imágenes (plano, gradiente, ruidosa).
- NO añadir umbrales hardcodeados que no escalen con la resolución
  (causa D).
- NO usar métricas de color alternativas sin cachear y sin validar que
  mejoran distRGB (causa B: Lab sin cachear amplificó ruido).
- NO aplicar despeckles sobre máscaras parciales de capas (causa C:
  los huecos son peores que el ruido).
- NO cambiar defaults sin tabla de medición (smooth ON por defecto
  costó distRGB 6.93 vs 1.80 en el gato).
- NO refinar la paleta (k-means/Lloyd) DESPUÉS de un paso de merge sin
  re-imponer separación mínima (causa A).

## 4.4 Reportar a upstream (GitLab de Inkscape)

Los hallazgos con parches mínimos y tests como evidencia:

1. `stack-union` (cuadro blanco en Multiple scans stack) — reproducible
   upstream byte a byte; el fix invierte la acumulación.
2. `palette-sort` (slots inválidos ordenados como negros fantasma).
3. `prune-nullcheck` (crash con imagen 100 % transparente).

Proceso: un MR por fix, contra `src/trace/`, con el caso de prueba
reducido (nuestros tests `alpha-sentinel`, `flat-zone` sirven como
base), referenciando el commit del tag analizado
(`dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871`).
