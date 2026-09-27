// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <cassert>
#include "imagemap-gdk.h"

namespace Inkscape {
namespace Trace {

GrayMap gdkPixbufToGrayMap(Glib::RefPtr<Gdk::Pixbuf> const &buf)
{
    int width     = buf->get_width();
    int height    = buf->get_height();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    auto map = GrayMap(width, height);

    bool const has_alpha = nchannels == 4;
    if (has_alpha) {
        // IVF PATCH (alpha): carry the alpha channel through instead of
        // compositing over white. Inkscape composited transparent pixels to
        // white here, which made quantizers/phasers treat transparency as an
        // opaque white background (see docs/FIX_FONDO.md).
        map.alpha.assign(size_t(width) * height, 255);
    }

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            if (has_alpha) {
                map.alpha[map.offset(x, y)] = p[3];
            }
            // IVF PATCH (alpha-nopremult): store the un-composited luma.
            // Upstream mixed toward white with `sample * a / 256 + 3*(255-a)`,
            // which turned anti-aliased transparent edges into phantom
            // near-white pixels. The alpha channel travels in the parallel
            // map and is applied as a weight by the consumers that need it.
            unsigned long sample = (int)p[0] + (int)p[1] + (int)p[2];
            map.setPixel(x, y, sample);
            p += nchannels;
        }
    }

    return map;
}

Glib::RefPtr<Gdk::Pixbuf> grayMapToGdkPixbuf(GrayMap const &map)
{
    auto buf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, map.width, map.height);

    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    for (int y = 0; y < map.height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < map.width; x++) {
            unsigned long pix = map.getPixel(x, y) / 3;
            p[0] = p[1] = p[2] = pix & 0xff;
            p += nchannels;
        }
    }

    return buf;
}

RgbMap gdkPixbufToRgbMap(Glib::RefPtr<Gdk::Pixbuf> const &buf)
{
    int width     = buf->get_width();
    int height    = buf->get_height();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    auto map = RgbMap(width, height);

    bool const has_alpha = nchannels == 4;
    if (has_alpha) {
        // IVF PATCH (alpha): carry the alpha channel through instead of
        // compositing over white (see docs/FIX_FONDO.md).
        map.alpha.assign(size_t(width) * height, 255);
    }

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            if (has_alpha) {
                map.alpha[map.offset(x, y)] = p[3];
            }
            // IVF PATCH (alpha-nopremult): do NOT pre-mix semi-transparent
            // pixels with white. Upstream stored r*a/256 + (255-a), which
            // pushed anti-aliased edges toward near-white garbage palette
            // entries (and slightly darkened even opaque pixels via /256).
            // The original color is stored untouched; `alpha` travels in the
            // parallel map and is applied as the histogram weight in
            // octreeBuildArea/octreeIndex.
            map.setPixel(x, y, {p[0], p[1], p[2]});
            p += nchannels;
        }
    }

    return map;
}

Glib::RefPtr<Gdk::Pixbuf> indexedMapToGdkPixbuf(IndexedMap const &map)
{
    // IVF PATCH (preview-alpha): produce an RGBA pixbuf when the source had
    // alpha. Fully transparent pixels (stored with the sentinel index
    // (unsigned)-1) must render as transparent, not as an arbitrary palette
    // entry via getPixelValue's modulo wrap (which made transparent areas
    // show up black in previews).
    bool const with_alpha = map.hasAlpha();
    auto buf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, with_alpha, 8, map.width, map.height);

    auto data     = buf->get_pixels();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();

    for (int y = 0; y < map.height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < map.width; x++) {
            auto raw = map.getPixel(x, y);
            if (with_alpha) {
                int const a = map.alpha[map.offset(x, y)];
                if (a == 0 || raw == (unsigned)-1) {
                    p[0] = p[1] = p[2] = 0;
                    p[3] = 0;
                    p += nchannels;
                    continue;
                }
                auto rgb = map.clut[raw % map.clut.size()];
                p[0] = rgb.r & 0xff;
                p[1] = rgb.g & 0xff;
                p[2] = rgb.b & 0xff;
                p[3] = a & 0xff;
            } else {
                auto rgb = map.getPixelValue(x, y);
                p[0] = rgb.r & 0xff;
                p[1] = rgb.g & 0xff;
                p[2] = rgb.b & 0xff;
            }
            p += nchannels;
        }
    }

    return buf;
}

} // namespace Trace
} // namespace Inkscape
