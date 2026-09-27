// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Image loading for InkscapeVectorizerFull (v1.2 — simplified).
 *
 * New code (not extracted from Inkscape). Replaces the input half of
 * Inkscape's tracing pipeline: in Inkscape the bitmap arrives as an
 * Inkscape::Pixbuf attached to an SPImage selected in the document; here
 * it is loaded directly from a file with gdk-pixbuf.
 *
 * v1.2: gamma/grayscale/max-size preprocessing removed (scope cut).
 * Alpha channels are preserved untouched (see docs/FIX_FONDO.md).
 */

#include "imageio.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

namespace ivf {

Glib::RefPtr<Gdk::Pixbuf> load_image(std::string const &filename)
{
    auto pixbuf = Gdk::Pixbuf::create_from_file(filename);
    if (!pixbuf) {
        return {};
    }

    // IVF PATCH (ga-la-expand): the converters assume 3 or 4 channels;
    // grayscale PNGs (G/GA) arrive with 1-2 and would be read out of
    // bounds. add_alpha() expands anything to RGBA (no-op cost for
    // already-RGBA images is avoided by the channels check).
    if (pixbuf->get_n_channels() < 3) {
        pixbuf = pixbuf->add_alpha(false, 0, 0, 0);
        if (!pixbuf) {
            return {};
        }
    }

    // The tracing engines index pixels manually; hand them a packed buffer.
    // (create_from_file can return a padded rowstride for some formats.)
    // IVF PATCH (packed-copy): copy() repacks rows without resampling
    // pixels; scale_simple() runs the bilinear filter even at identical
    // dimensions and is not guaranteed to be a no-op.
    if (pixbuf->get_rowstride() != pixbuf->get_width() * pixbuf->get_n_channels()) {
        pixbuf = pixbuf->copy();
    }
    return pixbuf;
}

bool has_alpha(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    return pixbuf && pixbuf->get_has_alpha();
}

} // namespace ivf
