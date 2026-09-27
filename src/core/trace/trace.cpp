// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Standalone replacement for the document-insertion half of Inkscape's
 * src/trace/trace.cpp (TraceTask::do_final_work). Generates SVG output
 * from tracing results without any Inkscape dependency.
 *
 * Derived from Inkscape, tag INKSCAPE_1_4_4, commit
 * dcaf3e7d9e6724cd18d6bd2b4f3d4f12ab691871.
 * Copyright (C) 2004-2022 Inkscape Authors
 */

#include "trace.h"

#include <sstream>
#include <iomanip>
#include <glib.h>
#include <2geom/svg-path-writer.h>
#include <2geom/path-sink.h>

namespace Inkscape {
namespace Trace {

// Standalone equivalent of Inkscape's count_pathvector_nodes (helper/geom).
static long count_nodes(Geom::PathVector const &pv)
{
    long count = 0;
    for (auto const &path : pv) {
        count += path.size(); // segments
        if (!path.empty()) {
            count++; // closing segment/point
        }
    }
    return count;
}

void SvgBuilder::addPath(std::string style, Geom::PathVector path)
{
    _node_count += count_nodes(path);
    _items.emplace_back(std::move(style), std::move(path));
}

std::string SvgBuilder::getSVGElements() const
{
    std::ostringstream out;
    for (auto const &item : _items) {
        out << "  <path style=\"" << item.style << "\" d=\""
            << write_svg_path(item.path, 8, true) << "\"/>\n";
    }
    return out.str();
}

std::string SvgBuilder::getSVG(double width, double height) const
{
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << width << "\" height=\"" << height << "\" "
        << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    if (_items.size() > 1) {
        out << "  <g>\n";
        std::istringstream in(getSVGElements());
        std::string line;
        while (std::getline(in, line)) {
            out << "  " << line << "\n";
        }
        out << "  </g>\n";
    } else {
        out << getSVGElements();
    }

    out << "</svg>\n";
    return out.str();
}

} // namespace Trace
} // namespace Inkscape
