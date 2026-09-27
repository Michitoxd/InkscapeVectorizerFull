// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Quantization for Inkscape
 *
 * Authors:
 *   Stéphane Gimenez <dev@gim.name>
 *
 * Copyright (C) 2006 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef INKSCAPE_TRACE_QUANTIZE_H
#define INKSCAPE_TRACE_QUANTIZE_H

#include "imagemap.h"

namespace Inkscape {
namespace Trace {

/**
 * Quantize an RGB image to a reduced number of colors.
 *
 * @param despeckle_island max area (px) of a connected same-index island that
 * is absorbed into its dominant neighboring index instead of surviving as an
 * isolated speckle (IVF PATCH (index-despeckle)). 0 disables the pass.
 */
IndexedMap rgbMapQuantize(RgbMap const &rgbmap, int nrColors, int despeckle_island = 0);

} // namespace Trace
} // namespace Inkscape

#endif // INKSCAPE_TRACE_QUANTIZE_H
