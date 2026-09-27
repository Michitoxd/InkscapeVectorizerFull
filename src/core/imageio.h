// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Image loading for InkscapeVectorizerFull (v1.2 — simplified).
 * New code (not extracted from Inkscape). See imageio.cpp.
 */

#ifndef IVF_IMAGEIO_H
#define IVF_IMAGEIO_H

#include <string>
#include <gdkmm/pixbuf.h>

namespace ivf {

/**
 * Load any gdk-pixbuf-supported image format (PNG, JPEG, BMP, GIF, TIFF,
 * WebP...). Alpha channels are preserved. Returns an empty RefPtr on error
 * (use Glib::Error handling at the caller if needed — create_from_file
 * throws on failure).
 */
Glib::RefPtr<Gdk::Pixbuf> load_image(std::string const &filename);

/// True if the pixbuf has an alpha channel.
bool has_alpha(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);

} // namespace ivf

#endif // IVF_IMAGEIO_H
