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
#ifndef INKSCAPE_TRACE_FILTERSET_H
#define INKSCAPE_TRACE_FILTERSET_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include "imagemap.h"

namespace Inkscape {
namespace Trace {

/**
 * Apply gaussian blur to an GrayMap.
 */
GrayMap grayMapGaussian(GrayMap const &gmap);

/**
 * Apply gaussian blur to an RgbMap.
 */
RgbMap rgbMapGaussian(RgbMap const &rgbmap);

GrayMap grayMapCanny(GrayMap const &gmap, double lowThreshold, double highThreshold);

GrayMap quantizeBand(RgbMap const &rgbmap, int nrColors);

/**
 * IVF PATCH (mono-despeckle): absorb connected islands of BLACK (or WHITE)
 * pixels smaller than max_island into the surrounding value.
 *
 * The mono pipeline binarizes before Potrace; antialiasing residue and JPEG
 * noise leave 1-16 px black specks that turdsize alone (contour-area based,
 * default 2) still traces as spurious dots. This pass is spatially coherent
 * (a whole island disappears, not its outline) and cannot damage real
 * features, which are connected to large regions. max_island <= 0 disables
 * the pass.
 */
GrayMap grayMapDespeckle(GrayMap const &gmap, int max_island);

} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_FILTERSET_H
