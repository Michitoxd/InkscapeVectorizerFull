// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Standalone replacement for Inkscape's src/trace/trace.h.
 *
 * This is a derivative work of the file of the same name from Inkscape
 * (https://gitlab.com/inkscape/inkscape), tag INKSCAPE_1_4_4, commit
 * dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871.
 *
 * Original authors: Bob Jamison <rjamison@titan.com>
 * Copyright (C) 2004-2022 Inkscape Authors
 *
 * CHANGES vs. the original (decoupling from the Inkscape application):
 *  - Removed all dependencies on the Inkscape document model (SPImage,
 *    SPItem, SPDocument, Selection, DocumentUndo, message stacks) and on
 *    the Inkscape async task framework (Async::Channel, TraceFuture,
 *    TraceTask). Tracing is now a synchronous call that the embedder can
 *    run on its own worker thread.
 *  - The SIOX foreground-extraction path (which in Inkscape rasterizes
 *    shapes stacked above the image) is not part of the standalone core.
 *  - Added SvgBuilder, which turns a TraceResult into SVG elements
 *    (in Inkscape this job was done by TraceTask::do_final_work writing
 *    XML nodes into the document).
 */

#ifndef IVF_TRACE_H
#define IVF_TRACE_H

#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <2geom/pathvector.h>
#include <gdkmm/pixbuf.h>

#include "async/progress.h"

namespace Inkscape {

namespace Async { template <typename... T> class Progress; }

namespace Trace {

struct TraceResultItem
{
    TraceResultItem(std::string style_, Geom::PathVector path_)
        : style(std::move(style_))
        , path(std::move(path_)) {}

    std::string style;      // e.g. "fill:#1a2b3c" or "stroke:#000000;fill:none"
    Geom::PathVector path;  // vector outline in pixel coordinates of the input image
};

using TraceResult = std::vector<TraceResultItem>;

/**
 * A generic interface for plugging different autotracers into the app.
 * (Unchanged interface contract from Inkscape.)
 */
class TracingEngine
{
public:
    TracingEngine() = default;
    virtual ~TracingEngine() = default;

    /**
     * Take a GdkPixbuf, trace it, and return a style attribute and path data
     * compatible with the d="" attribute of an SVG <path> element.
     * This function may be called off-main-thread, so it must be thread-safe.
     */
    virtual TraceResult trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress) = 0;

    /**
     * Generate a quick preview without any actual tracing. Like trace(),
     * this must be thread-safe.
     */
    virtual Glib::RefPtr<Gdk::Pixbuf> preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) = 0;

    /**
     * Return true if the user should be warned before tracing because the image is too big.
     */
    virtual bool check_image_size(Geom::IntPoint const &size) const { return false; }
};

/**
 * Accumulates TraceResultItems and serializes them as SVG elements.
 * Standalone replacement for TraceTask::do_final_work().
 */
class SvgBuilder
{
public:
    void addPath(std::string style, Geom::PathVector path);

    /// Number of accumulated <path> elements.
    std::size_t size() const { return _items.size(); }
    /// Total number of SVG nodes (curves) accumulated.
    long nodeCount() const { return _node_count; }

    /// Serialize everything accumulated so far as a standalone SVG document.
    /// @param width  width of the SVG viewport in user units (usually pixel width of the input image)
    /// @param height height of the SVG viewport in user units
    std::string getSVG(double width, double height) const;

    /// Serialize only the path elements (no <svg> wrapper). Useful for tests.
    std::string getSVGElements() const;

    TraceResult const &result() const { return _items; }

private:
    TraceResult _items;
    long _node_count = 0;
};

} // namespace Trace
} // namespace Inkscape

#endif // IVF_TRACE_H
