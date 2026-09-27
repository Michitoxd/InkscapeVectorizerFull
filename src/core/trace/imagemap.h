// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef INKSCAPE_TRACE_IMAGEMAP_H
#define INKSCAPE_TRACE_IMAGEMAP_H

#include <vector>
#include <array>

namespace Inkscape {
namespace Trace {

template <typename T>
struct MapBase
{
    int width;
    int height;
    std::vector<T> pixels;
    std::vector<unsigned char> alpha; ///< IVF PATCH (alpha): parallel per-pixel alpha (0..255); empty means "opaque".

    MapBase(int width, int height)
        : width(width)
        , height(height)
        , pixels(width * height) {}

    bool hasAlpha() const { return !alpha.empty(); }
    unsigned char getAlpha(int x, int y) const { return hasAlpha() ? alpha[offset(x, y)] : 255; }
    /// IVF PATCH (alpha-threshold): background test — alpha below the 50%
    /// cutoff. Shared by RgbMap and IndexedMap consumers.
    bool isTransparent(int x, int y) const
    {
        return hasAlpha() && alpha[offset(x, y)] < ALPHA_THRESHOLD;
    }

    // IVF PATCH (alpha-threshold): conventional 50% coverage cutoff. Pixels
    // below this alpha are BACKGROUND (excluded from quantization, sentinel
    // in the IndexedMap), not solid content. Treating alpha 1..127 as solid
    // was the root cause of the "puntos negros" bug: the anti-aliased edge
    // of a dark region against transparency produced barely-visible pixels
    // that were quantized as full black and traced as specks.
    static constexpr unsigned char ALPHA_THRESHOLD = 128;
    void setAlpha(int x, int y, unsigned char a)
    {
        if (alpha.empty()) alpha.assign(pixels.size(), 255);
        alpha[offset(x, y)] = a;
    }

    int offset(int x, int y) const { return x + y * width; }
    T       *row(int y)       { return pixels.data() + y * width; }
    T const *row(int y) const { return pixels.data() + y * width; }
    void setPixel(int x, int y, T val) { pixels[offset(x, y)] = val; }
    T getPixel(int x, int y) const { return pixels[offset(x, y)]; }
};

/*
 * GrayMap
 */

struct GrayMap
    : MapBase<unsigned long>
{
    static unsigned long constexpr BLACK = 0;
    static unsigned long constexpr WHITE = 255 * 3;

    GrayMap(int width, int height);

    bool writePPM(char const *fileName);
};

/*
 * RgbMap
 */

struct RGB
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

struct RgbMap
    : MapBase<RGB>
{
    RgbMap(int width, int height);

    bool writePPM(char const *fileName);
};

/*
 * IndexedMap
 */

struct IndexedMap
    : MapBase<unsigned>
{
    IndexedMap(int width, int height);

    RGB getPixelValue(int x, int y) const { return clut[getPixel(x, y) % clut.size()]; }
    /// IVF PATCH (is-transparent): authoritative transparency test via the
    /// parallel alpha map (inherited from MapBase; kept for readability).
    /// Consumers must use this (or the (unsigned)-1 equality check) BEFORE any ordered comparison of indices: the
    /// sentinel (unsigned)-1 is >= every real index and stack-union's
    /// `index >= colorIndex` would otherwise paint transparent pixels black
    /// (docs/AUDITORIA_v3.md 0.1-A, docs/FIX_FONDO.md).
    bool isTransparent(int x, int y) const
    {
        return hasAlpha() && alpha[offset(x, y)] < ALPHA_THRESHOLD;
    }
    bool writePPM(char const *fileName);

    int nrColors;
    std::array<RGB, 256> clut; ///< Color look-up table.
};

} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_IMAGEMAP_H
