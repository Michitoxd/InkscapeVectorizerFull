// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Some filters for Potrace in Inkscape
 *
 * Authors:
 *   Bob Jamison <rjamison@titan.com>
 *   Stéphane Gimenez <dev@gim.name>
 *
 * Copyright (C) 2004-2006 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <cstdio>
#include "imagemap-gdk.h"
#include "filterset.h"
#include "quantize.h"

namespace Inkscape {
namespace Trace {

// IVF PATCH (gaussian-border): reflected coordinate for border convolution
// (docs/RECOMENDACIONES.md R1). i in [0, size) is returned as-is.
static inline int reflect(int i, int size)
{
    if (i < 0) return -i - 1;
    if (i >= size) return 2 * size - i - 1;
    return i;
}

/*#########################################################################
### G A U S S I A N  (smoothing)
#########################################################################*/

static int gaussMatrix[] =
{
    2,  4,  5,  4, 2,
    4,  9, 12,  9, 4,
    5, 12, 15, 12, 5,
    4,  9, 12,  9, 4,
    2,  4,  5,  4, 2
};

GrayMap grayMapGaussian(GrayMap const &me) // Todo: Make member function, keep implementation here
{
    int width  = me.width;
    int height = me.height;

    auto newGm = GrayMap(width, height);

    // IVF PATCH (gaussian-alpha): preserve the parallel alpha map through
    // the blur (same rationale as rgbMapGaussian above).
    if (me.hasAlpha()) {
        newGm.alpha = me.alpha;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // IVF PATCH (gaussian-border): convolve the borders too, using
            // mirrored (reflected) coordinates instead of copying the raw
            // pixel. The old 2 px unfiltered band was visible as ragged
            // edges on small images (docs/RECOMENDACIONES.md R1) and, with
            // mono-alpha-composite, could keep un-blurred ink next to the
            // frame while the interior was smoothed.
            unsigned long sum = 0;
            int gaussIndex = 0;
            for (int i = y - 2; i <= y + 2; i++) {
                int const yi = reflect(i, height);
                for (int j = x - 2; j <= x + 2; j++) {
                    sum += me.getPixel(reflect(j, width), yi) * gaussMatrix[gaussIndex++];
                }
            }
            sum /= 159;
            sum = std::min(sum, GrayMap::WHITE);
            newGm.setPixel(x, y, sum);
        }
    }

    return newGm;
}

RgbMap rgbMapGaussian(RgbMap const &me)
{
    int width  = me.width;
    int height = me.height;

    auto newGm = RgbMap(width, height);

    // IVF PATCH (gaussian-alpha): carry the parallel alpha map through the
    // blur. Upstream (and our v1.4 initial patch) dropped it, so fully
    // transparent pixels — whose raw RGB is typically (0,0,0) after
    // alpha-nopremult — entered the octree as OPAQUE BLACK and the whole
    // image traced as a black rectangle (docs/AUDITORIA_v3.md 0.1-B).
    // Transparent pixels travel with their own alpha; consumers apply it.
    if (me.hasAlpha()) {
        newGm.alpha = me.alpha;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // IVF PATCH (gaussian-border): mirrored borders, as in
            // grayMapGaussian (docs/RECOMENDACIONES.md R1).
            int gaussIndex = 0;
            int sumR       = 0;
            int sumG       = 0;
            int sumB       = 0;
            for (int i = y - 2; i <= y + 2; i++) {
                int const yi = reflect(i, height);
                for (int j = x - 2; j <= x + 2; j++) {
                    int weight = gaussMatrix[gaussIndex++];
                    RGB rgb = me.getPixel(reflect(j, width), yi);
                    sumR += weight * rgb.r;
                    sumG += weight * rgb.g;
                    sumB += weight * rgb.b;
                }
            }
            RGB rout;
            rout.r = (sumR / 159) & 0xff;
            rout.g = (sumG / 159) & 0xff;
            rout.b = (sumB / 159) & 0xff;
            newGm.setPixel(x, y, rout);
        }
    }

    return newGm;
}

/*#########################################################################
### C A N N Y    E D G E    D E T E C T I O N
#########################################################################*/

static int sobelX[] =
{
    -1,  0,  1 ,
    -2,  0,  2 ,
    -1,  0,  1 
};

static int sobelY[] =
{
     1,  2,  1 ,
     0,  0,  0 ,
    -1, -2, -1 
};

/**
 * Perform Sobel convolution on a GrayMap.
 */
GrayMap grayMapCanny(GrayMap const &gm, double dLowThreshold, double dHighThreshold)
{
    int width  = gm.width;
    int height = gm.height;
    int firstX = 1;
    int lastX  = width - 2;
    int firstY = 1;
    int lastY  = height - 2;

    auto map = GrayMap(width, height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            bool edge;
            // image boundaries
            if (x < firstX || x > lastX || y < firstY || y > lastY) {
                edge = false;
            } else {
                // SOBEL FILTERING
                long sumX = 0;
                long sumY = 0;
                int sobelIndex = 0;
                for (int i = y-1; i <= y + 1; i++) {
                    for (int j = x - 1; j <= x + 1; j++) {
                        sumX += gm.getPixel(j, i) * sobelX[sobelIndex++];
                    }
	            }

                sobelIndex = 0;
                for (int i = y - 1; i <= y + 1; i++) {
                    for (int j = x - 1; j <= x + 1; j++) {
                        sumY += gm.getPixel(j, i) * sobelY[sobelIndex++];
                    }
	            }

                // GET VALUE
                unsigned long sum = std::abs(sumX) + std::abs(sumY);
                sum = std::min(sum, GrayMap::WHITE);

                // GET EDGE DIRECTION (fast way)
                int edgeDirection = 0; // x, y = 0
                if (sumX == 0) {
                    if (sumY != 0) {
                        edgeDirection = 90;
                    }
                } else {
                    long slope = sumY * 1024 / sumX;
                    if (slope > 2472 || slope< -2472) { // tan(67.5) * 1024
                        edgeDirection = 90;
                    } else if (slope > 414) { // tan(22.5) * 1024
                        edgeDirection = 45;
                    } else if (slope < -414) { // -tan(22.5) * 1024
                        edgeDirection = 135;
                    }
                }

                // printf("%ld %ld %f %d\n", sumX, sumY, orient, edgeDirection);

                // Get two adjacent pixels in edge direction
                unsigned long leftPixel;
                unsigned long rightPixel;
                if (edgeDirection == 0) {
                    leftPixel  = gm.getPixel(x - 1, y);
                    rightPixel = gm.getPixel(x + 1, y);
                } else if (edgeDirection == 45) {
                    leftPixel  = gm.getPixel(x - 1, y + 1);
                    rightPixel = gm.getPixel(x + 1, y - 1);
                } else if (edgeDirection == 90) {
                    leftPixel  = gm.getPixel(x, y - 1);
                    rightPixel = gm.getPixel(x, y + 1);
                } else { // 135
                    leftPixel  = gm.getPixel(x - 1, y - 1);
                    rightPixel = gm.getPixel(x + 1, y + 1);
                }

                // Compare current value to adjacent pixels. (If less than either, suppress it.)
                if (sum < leftPixel || sum < rightPixel) {
                    edge = false;
                } else {
                    unsigned long highThreshold = dHighThreshold * GrayMap::WHITE;
                    unsigned long lowThreshold  = dLowThreshold  * GrayMap::WHITE;
                    if (sum >= highThreshold) {
                        edge = true;
                    } else if (sum < lowThreshold) {
                        edge = false;
                    } else {
                        edge = gm.getPixel(x - 1, y - 1) > highThreshold ||
                               gm.getPixel(x    , y - 1) > highThreshold ||
                               gm.getPixel(x + 1, y - 1) > highThreshold ||
                               gm.getPixel(x - 1, y    ) > highThreshold ||
                               gm.getPixel(x + 1, y    ) > highThreshold ||
                               gm.getPixel(x - 1, y + 1) > highThreshold ||
                               gm.getPixel(x    , y + 1) > highThreshold ||
                               gm.getPixel(x + 1, y + 1) > highThreshold;
                    }
                }
            }

            // show edges as dark over light
            map.setPixel(x, y, edge ? GrayMap::BLACK : GrayMap::WHITE);
        }
    }

    // map.writePPM("canny.ppm");
    return map;
}

/*#########################################################################
### Q U A N T I Z A T I O N
#########################################################################*/

GrayMap quantizeBand(RgbMap const &rgbMap, int nrColors)
{
    auto gaussMap = rgbMapGaussian(rgbMap);
    // gaussMap->writePPM(gaussMap, "rgbgauss.ppm");

    auto qMap = rgbMapQuantize(gaussMap, nrColors);
    // qMap->writePPM(qMap, "rgbquant.ppm");

    auto gm = GrayMap(rgbMap.width, rgbMap.height);

    // RGB is quantized. There should now be a small set of (R+G+B)
    for (int y = 0; y < qMap.height; y++) {
        for (int x = 0; x < qMap.width; x++) {
            auto rgb = qMap.getPixelValue(x, y);
            int sum = rgb.r + rgb.g + rgb.b;
            auto result = (sum & 1) ? GrayMap::WHITE : GrayMap::BLACK;
            // printf("%d %d %d : %d\n", rgb.r, rgb.g, rgb.b, index);
            gm.setPixel(x, y, result);
        }
    }

    return gm;
}

/*#########################################################################
### D E S P E C K L E   (mono binary islands)
#########################################################################*/

/*
 * IVF PATCH (mono-despeckle): 8-connected component labeling over the
 * binarized GrayMap. Islands of the minority value smaller than
 * max_island are repainted to the majority value of their border.
 * Mirrors the IndexedMap despeckle in quantize.cpp (index-despeckle)
 * but operates pre-trace on the binary image, so Potrace never sees
 * the specks at all.
 *
 * IVF PATCH (circular-despeckle) [v1.5.2]: adds the same isolation rule as
 * the color path. Size alone is not enough: an isolated noise blob of 20 px
 * was kept (above threshold) while it clearly has no same-color structure
 * anywhere near it. An island — small OR large — is only absorbed when no
 * pixel of its own value exists within ADJ_RADIUS (=2 px) of its bounding
 * box; that tiny radius only protects AA fringes touching their parent
 * region. [v1.5.3] fix: the radius used to be max_island, which protected
 * noise dots that shared the value of a nearby large region (black-frame
 * dots bug). An isolated island larger than iso_cap (8 × max_island) is
 * kept as a deliberate lone feature.
 */
GrayMap grayMapDespeckle(GrayMap const &gmap, int max_island)
{
    if (max_island <= 0 || gmap.width <= 0 || gmap.height <= 0) {
        return gmap;
    }

    int const w = gmap.width;
    int const h = gmap.height;
    GrayMap out(gmap.width, gmap.height);
    out.pixels = gmap.pixels;
    out.alpha = gmap.alpha;

    unsigned long const black = GrayMap::BLACK;
    unsigned long const white = GrayMap::WHITE;

    std::vector<char> visited(size_t(w) * h, 0);
    std::vector<char> inCell(size_t(w) * h, 0);
    std::vector<int> stack;
    std::vector<int> cells;

    int const dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    int const dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

    // See circular-despeckle note above: 1 px exemption radius. Touching
    // fringes are same-component anyway; radius >=2 protected noise dots
    // hugging same-colored regions.
    int const adj_radius = 1;
    auto sameValueNearby = [&](int minx, int miny, int maxx, int maxy, unsigned long v) {
        int const x1 = std::max(0, minx - adj_radius);
        int const y1 = std::max(0, miny - adj_radius);
        int const x2 = std::min(w - 1, maxx + adj_radius);
        int const y2 = std::min(h - 1, maxy + adj_radius);
        for (int yy = y1; yy <= y2; yy++) {
            for (int xx = x1; xx <= x2; xx++) {
                int const p = xx + yy * w;
                if (inCell[p]) {
                    continue;
                }
                if (gmap.getPixel(xx, yy) == v) {
                    return true;
                }
            }
        }
        return false;
    };

    for (int y0 = 0; y0 < h; y0++) {
        for (int x0 = 0; x0 < w; x0++) {
            int const start = x0 + y0 * w;
            unsigned long const val = gmap.getPixel(x0, y0);
            if (visited[start] || (val != black && val != white)) {
                continue;
            }
            int const self = (val == black) ? 0 : 1;

            cells.clear();
            stack.clear();
            stack.push_back(start);
            visited[start] = 1;
            while (!stack.empty()) {
                int const cur = stack.back();
                stack.pop_back();
                cells.push_back(cur);
                inCell[cur] = 1;
                int const cx = cur % w;
                int const cy = cur / w;
                for (int k = 0; k < 8; k++) {
                    int const nx = cx + dx[k];
                    int const ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    unsigned long const nval = gmap.getPixel(nx, ny);
                    if (nval == val) {
                        int const ni = nx + ny * w;
                        if (!visited[ni]) {
                            visited[ni] = 1;
                            stack.push_back(ni);
                        }
                    } else {
                        // border pixel of the opposite value; nothing to count
                    }
                }
            }

            int minx = w, maxx = -1, miny = h, maxy = -1;
            for (int c : cells) {
                int const cx = c % w;
                int const cy = c / w;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
            }

            bool const connected = sameValueNearby(minx, miny, maxx, maxy, val);
            bool const small = int(cells.size()) < max_island;
            int const iso_cap = max_island * 8;

            if (connected) {
                // Same-color pixels nearby: real feature, keep.
                for (int c : cells) {
                    inCell[c] = 0;
                }
                continue;
            }
            if (!small && int(cells.size()) > iso_cap) {
                // Large AND isolated: most likely a deliberate lone feature
                // (a real dot that means something). Keep.
                for (int c : cells) {
                    inCell[c] = 0;
                }
                continue;
            }
            // Isolated (no same-value pixels nearby) and under the cap:
            // a noise speck or blob. Absorb into the opposite value.
            unsigned long const other = (self == 0) ? white : black;
            for (int c : cells) {
                out.setPixel(c % w, c / w, other);
                inCell[c] = 0;
            }
        }
    }

    return out;
}

} // namespace Trace
} // namespace Inkscape
