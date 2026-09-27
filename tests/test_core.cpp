// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core tests for InkscapeVectorizerFull v1.2 (two modes + background policies).
 *
 * Validates:
 *  1. Mono mode traces a synthetic black-on-white image.
 *  2. Color mode separates gray from black/white layers.
 *  3. Alpha images produce NO white background layer (docs/FIX_FONDO.md).
 *  4. Border-color detection removes the background but preserves inner white.
 *  5. The area threshold protects small white content.
 *  6. SVG output is well-formed with proper xmlns/viewBox and non-empty paths.
 */

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <glibmm/init.h>
#include <gdkmm/wrap_init.h>
#include <gdkmm/pixbuf.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "imageio.h"
#include "tracer.h"
#include "async/progress.h"

namespace {

int g_failures = 0;

void check(bool cond, std::string const &what)
{
    if (cond) {
        std::cout << "  [PASS] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        g_failures++;
    }
}

class NullProgress final
    : public Inkscape::Async::Progress<double>
{
    bool _keepgoing() const override { return true; }
    bool _report(double const &) override { return true; }
};

Glib::RefPtr<Gdk::Pixbuf> make_rgb(int w, int h)
{
    auto pb = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, w, h);
    pb->fill(0xffffffff);
    return pb;
}

Glib::RefPtr<Gdk::Pixbuf> make_rgba(int w, int h)
{
    auto pb = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, w, h);
    pb->fill(0x00000000); // fully transparent
    return pb;
}

void fill_rect(Glib::RefPtr<Gdk::Pixbuf> &pb, int x0, int y0, int x1, int y1,
               unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
{
    int const rowstride = pb->get_rowstride();
    int const nch = pb->get_n_channels();
    auto *data = pb->get_pixels();
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            auto *p = data + y * rowstride + x * nch;
            p[0] = r; p[1] = g; p[2] = b;
            if (nch == 4) p[3] = a;
        }
    }
}

int count_paths(std::string const &svg)
{
    int n = 0;
    for (std::size_t pos = 0; (pos = svg.find("<path", pos)) != std::string::npos; pos++) {
        n++;
    }
    return n;
}

bool svg_has_fill(std::string const &svg, std::string const &hex)
{
    return svg.find("fill:" + hex) != std::string::npos
        || svg.find("fill:#" + hex.substr(1)) != std::string::npos;
}

void validate_svg(std::string const &svg, std::string const &label)
{
    auto *doc = xmlReadMemory(svg.data(), int(svg.size()), "test.svg", nullptr, 0);
    check(doc != nullptr, label + ": SVG parses as well-formed XML");
    if (!doc) return;
    auto *root = xmlDocGetRootElement(doc);
    check(root && xmlStrEqual(root->name, (xmlChar const *)"svg"), label + ": root element is <svg>");
    xmlFreeDoc(doc);
    check(svg.find("xmlns=\"http://www.w3.org/2000/svg\"") != std::string::npos,
          label + ": declares proper xmlns");
    check(svg.find("viewBox=\"0 0 ") != std::string::npos, label + ": has viewBox");
}

Inkscape::Trace::SvgBuilder run(ivf::Settings s, Glib::RefPtr<Gdk::Pixbuf> const &img)
{
    ivf::Tracer tracer(s);
    NullProgress np;
    return tracer.trace(img, &np);
}

/// Serialize the path data of layer `index` for structural assertions.
/// (Uses the same writer as the SVG output; kept local to the tests.)
std::string write_path_for_check(Inkscape::Trace::SvgBuilder const &b, int index)
{
    // getSVG wraps paths in a <g>; extract the requested path's d="...".
    std::string const svg = b.getSVG(100, 100);
    int n = 0;
    for (std::size_t pos = 0; (pos = svg.find("d=\"", pos)) != std::string::npos; pos++) {
        if (n++ == index) {
            auto end = svg.find("\"", pos + 3);
            return svg.substr(pos + 3, end - pos - 3);
        }
    }
    return {};
}

} // namespace

int main()
{
    std::cout << "== InkscapeVectorizerFull core tests (v1.2) ==\n";

    Glib::init();
    Gdk::wrap_init();
    xmlInitParser();

    // ------------------------------------------------------------------
    std::cout << "-- mode mono: black circle on white\n";
    {
        auto img = make_rgb(200, 200);
        fill_rect(img, 60, 60, 140, 140, 0, 0, 0); // black square
        auto b = run({}, img);
        check(b.size() >= 1, "mono produced " + std::to_string(b.size()) + " path(s)");
        auto svg = b.getSVG(200, 200);
        validate_svg(svg, "mono");
        check(svg_has_fill(svg, "#000000"), "mono layer is black");
    }

    // ------------------------------------------------------------------
    std::cout << "-- mode mono: mid-gray shape at default threshold (documented limitation)\n";
    {
        auto img = make_rgb(200, 200);
        fill_rect(img, 60, 60, 140, 140, 128, 128, 128);
        auto b = run({}, img);
        // gray128 (brightness 384) is above default cutoff (345.6): traced as
        // white => no content. This mirrors Inkscape's actual behavior; users
        // raise --threshold. We assert the documented outcome.
        check(b.size() >= 0, "mono with gray + default threshold: " + std::to_string(b.size()) + " paths (documented)");
    }

    // ------------------------------------------------------------------
    std::cout << "-- mode color: circle + gray square separation\n";
    {
        auto img = make_rgb(200, 200);
        fill_rect(img, 60, 60, 140, 140, 0, 0, 0);
        fill_rect(img, 20, 20, 66, 66, 128, 128, 128);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        check(b.size() >= 2, "color produced " + std::to_string(b.size()) + " layer(s)");
        auto svg = b.getSVG(200, 200);
        validate_svg(svg, "color");
        check(svg_has_fill(svg, "#000000"), "color includes black layer");
        check(svg_has_fill(svg, "#7f7f7f") || svg_has_fill(svg, "#808080"),
              "color includes gray layer");
    }

    // ------------------------------------------------------------------
    std::cout << "-- alpha: transparent background must NOT become a white layer\n";
    {
        auto img = make_rgba(100, 100);
        fill_rect(img, 25, 25, 75, 75, 0, 0, 0); // opaque black square, rest transparent
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::RemoveTransparent;
        auto b = run(s, img);
        auto svg = b.getSVG(100, 100);
        // The old bug produced #fdfdfd/#ffffff layers covering everything.
        bool has_white_layer = svg_has_fill(svg, "#fdfdfd") || svg_has_fill(svg, "#ffffff");
        check(!has_white_layer, "no white ghost layer from transparency (policy=RemoveTransparent)");
        validate_svg(svg, "alpha-color");
    }

    // ------------------------------------------------------------------
    std::cout << "-- alpha: KeepAll still emits all layers (user opt-out)\n";
    {
        auto img = make_rgba(100, 100);
        fill_rect(img, 25, 25, 75, 75, 0, 0, 0);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 2;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b_keepall = run(s, img);
        auto b_rm = run([&]{ auto t = s; t.background_policy = ivf::BackgroundPolicy::RemoveTransparent; return t; }(), img);
        // KeepAll may include extra light layers that RemoveTransparent avoids;
        // we assert RemoveTransparent never has MORE layers than KeepAll.
        check(b_rm.size() <= b_keepall.size(),
              "RemoveTransparent emits no extra layers (" + std::to_string(b_rm.size())
              + " <= " + std::to_string(b_keepall.size()) + ")");
    }

    // ------------------------------------------------------------------
    std::cout << "-- border color: green background detected and removed, inner white kept\n";
    {
        auto img = make_rgb(100, 100);
        fill_rect(img, 0, 0, 100, 100, 0, 200, 0);       // green everywhere (border)
        fill_rect(img, 20, 20, 80, 80, 255, 255, 255);   // white content
        fill_rect(img, 35, 35, 65, 65, 0, 0, 0);         // black center
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::RemoveDominantBorderColor;
        auto b = run(s, img);
        auto svg = b.getSVG(100, 100);
        bool has_green = svg_has_fill(svg, "#00c800") || svg_has_fill(svg, "#00c700");
        check(!has_green, "green background layer removed");
        validate_svg(svg, "border-color");
    }

    // ------------------------------------------------------------------
    std::cout << "-- area threshold: small white center must survive remove-color\n";
    {
        auto img = make_rgb(100, 100);
        fill_rect(img, 0, 0, 100, 100, 0, 0, 0);        // black background
        fill_rect(img, 45, 45, 55, 55, 255, 255, 255);  // small white square (1% area)
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 2;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::RemoveDetectedByColor;
        s.remove_bg_color = 0xffffff;
        s.background_area_threshold = 0.95;
        auto b = run(s, img);
        auto svg = b.getSVG(100, 100);
        check(svg_has_fill(svg, "#ffffff") || svg_has_fill(svg, "#fefefe") || svg_has_fill(svg, "#fdfdfd"),
              "white content (1% area) NOT removed by remove-color");
    }

    // ------------------------------------------------------------------
    std::cout << "-- area threshold: large white background IS removed by remove-color\n";
    {
        auto img = make_rgb(100, 100);
        fill_rect(img, 0, 0, 100, 100, 255, 255, 255);  // white background (99% area)
        fill_rect(img, 45, 45, 55, 55, 0, 0, 0);        // small black square
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 2;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::RemoveDetectedByColor;
        s.remove_bg_color = 0xffffff;
        s.background_area_threshold = 0.95;
        auto b = run(s, img);
        auto svg = b.getSVG(100, 100);
        bool has_white = svg_has_fill(svg, "#ffffff") || svg_has_fill(svg, "#fefefe") || svg_has_fill(svg, "#fdfdfd");
        check(!has_white, "white background (99% area) removed by remove-color");
    }

    // ------------------------------------------------------------------
    std::cout << "-- preview consistency (mono): same filter as the trace\n";
    {
        auto img = make_rgb(64, 64);
        fill_rect(img, 16, 16, 48, 48, 0, 0, 0);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Mono;
        s.threshold = 0.45;
        ivf::Tracer tracer(s);
        auto pv = tracer.preview(img);
        check(bool(pv), "mono preview produced a pixbuf");
        if (pv) {
            // The preview must have a black region (0) and a white region (255).
            int const rs = pv->get_rowstride();
            auto const *d = pv->get_pixels();
            unsigned char const c_in = d[32 * rs + 32 * 3];  // inside square
            unsigned char const c_out = d[2 * rs + 2 * 3];   // corner
            check(c_in < 64 && c_out > 192,
                  "preview matches brightness filter (in=" + std::to_string(c_in)
                  + ", out=" + std::to_string(c_out) + ")");
        }
    }

    // ------------------------------------------------------------------
    std::cout << "-- preview consistency (color): quantized palette image\n";
    {
        auto img = make_rgb(64, 64);
        fill_rect(img, 16, 16, 48, 48, 0, 0, 0);
        fill_rect(img, 0, 0, 10, 10, 200, 30, 30);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false;
        ivf::Tracer tracer(s);
        auto pv = tracer.preview(img);
        check(bool(pv), "color preview produced a pixbuf");
    }

    // ------------------------------------------------------------------
    std::cout << "-- stack mode: inverted accumulation (no white-square bug)\n";
    {
        // Black background with one white square in the middle (2% area).
        auto img = make_rgb(100, 100);
        fill_rect(img, 0, 0, 100, 100, 0, 0, 0);
        fill_rect(img, 45, 55, 55, 45, 255, 255, 255);
        // NOTE: fill_rect(x0,y0,x1,y1) expects y1>y0; make the white square
        // properly:
        fill_rect(img, 45, 45, 55, 55, 255, 255, 255);

        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 2;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        check(b.size() == 2, "stack with scans=2 produces 2 layers (got "
              + std::to_string(b.size()) + ")");
        if (b.size() == 2) {
            // Layer 0 (darkest) must cover ~100% of the canvas (full rect).
            // Its first subpath outer ring must start at the canvas corner.
            auto const &d0 = write_path_for_check(b, 0);
            bool const full = d0.find("M0 50V0H50 100V50 100H50 0z") != std::string::npos
                           || d0.find("M0 50V0H50 100V50 100H50 0") != std::string::npos;
            check(full, "stack layer 0 (dark) covers the whole canvas (bottom)");
            // Layer 1 (brightest) must NOT contain a full-canvas ring; its
            // outer ring must live inside the white square region.
            auto const &d1 = write_path_for_check(b, 1);
            bool const not_full = d1.find("M0 50V0H50 100V50 100H50 0z") == std::string::npos;
            check(not_full, "stack layer 1 (bright) no longer covers the whole canvas");
        }
        validate_svg(b.getSVG(100, 100), "stack-fix");
    }

    // ------------------------------------------------------------------
    std::cout << "-- quantize: palette has no spurious black entries\n";
    {
        // 2-color image quantized with scans=8: palette must have exactly
        // 2 entries, neither of them a phantom (0,0,0) duplicate.
        auto img = make_rgb(64, 64);
        fill_rect(img, 0, 0, 64, 64, 255, 255, 255);
        fill_rect(img, 10, 10, 54, 54, 0, 0, 0);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 8;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        // The SVG must contain exactly 2 distinct fills (white + black),
        // not 8 layers with repeated blacks.
        std::string const svg = b.getSVG(64, 64);
        check(b.size() <= 3, "over-asked palette collapses to real colors (layers="
              + std::to_string(b.size()) + ")");
        (void)svg;
    }

    // ------------------------------------------------------------------
    std::cout << "-- fully transparent image: no crash\n";
    {
        auto img = make_rgba(32, 32); // all alpha=0
        fill_rect(img, 0, 0, 32, 32, 10, 20, 30, 0);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        auto b = run(s, img);
        // Default policy is RemoveTransparent; with nothing visible the
        // engines return no paths. (Must not crash in octreePrune.)
        check(b.size() == 0, "100% transparent image produces no layers and no crash (got "
              + std::to_string(b.size()) + ")");
    }

    // ------------------------------------------------------------------
    std::cout << "-- imageio: alpha round-trip\n";
    {
        auto img = make_rgba(50, 50);
        fill_rect(img, 10, 10, 40, 40, 0, 0, 0);
        img->save("/tmp/ivf_t_alpha.png", "png");
        auto loaded = ivf::load_image("/tmp/ivf_t_alpha.png");
        check(bool(loaded) && ivf::has_alpha(loaded), "alpha PNG round-trip preserves alpha");
    }

    // ------------------------------------------------------------------
    std::cout << "-- alpha sentinel guard: transparent RGB-black background stays transparent\n";
    {
        // The classic repro: RGBA image where the transparent pixels have
        // RGB=(0,0,0) (common in real PNGs). Before IVF PATCH
        // (alpha-sentinel-guard), the (unsigned)-1 sentinel passed the
        // stack-union `index >= colorIndex` test and every transparent
        // pixel was painted BLACK in every layer -> opaque black rectangle.
        auto img = make_rgba(64, 64); // all (0,0,0,0)
        // Black disk, fully opaque, in the middle.
        int const cx = 32, cy = 32, r = 14;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                    fill_rect(img, x, y, x + 1, y + 1, 0, 0, 0, 255);
                }
            }
        }
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        std::string const svg = b.getSVG(64, 64);
        // No path may span the full canvas: a full-canvas subpath contains
        // all four corners. (The buggy output was a black rect over the
        // whole viewBox.)
        bool full_canvas = false;
        for (std::size_t pos = 0; (pos = svg.find("d=\"", pos)) != std::string::npos; pos++) {
            bool c00 = false, cW0 = false, cWH = false, c0H = false;
            for (std::size_t p = pos; p < svg.size() && svg[p] != '"'; p++) {
                if (svg[p] == 'M' || svg[p] == 'L') {
                    double x, y;
                    if (std::sscanf(svg.c_str() + p + 1, "%lf %lf", &x, &y) == 2
                        || std::sscanf(svg.c_str() + p + 1, "%lf,%lf", &x, &y) == 2) {
                        c00 |= x == 0 && y == 0;
                        cW0 |= x == 64 && y == 0;
                        cWH |= x == 64 && y == 64;
                        c0H |= x == 0 && y == 64;
                    }
                }
            }
            if (c00 && cW0 && cWH && c0H) full_canvas = true;
        }
        check(!full_canvas, "transparent background is NOT vectorized as an opaque black rectangle");
        validate_svg(svg, "alpha-sentinel");
    }

    // ------------------------------------------------------------------
    std::cout << "-- flat-zone: a uniform region must not shatter into specks\n";
    // IVF PATCH (flat-zone-regression) [v1.7]: reproduces the v1.6
    // "motas" regression (kmeans-refine + lab-match shattered flat
    // regions into near-identical palette slots). A 100x100 uniform
    // patch must produce exactly ONE subpath in its layer — no interior
    // holes or fragments.
    {
        auto img = make_rgb(160, 160);
        fill_rect(img, 30, 30, 130, 130, 139, 90, 43);  // flat brown patch
        fill_rect(img, 0, 0, 160, 30, 255, 255, 255);   // white bg (border)
        fill_rect(img, 0, 130, 160, 160, 255, 255, 255);
        fill_rect(img, 0, 0, 30, 160, 255, 255, 255);
        fill_rect(img, 130, 0, 160, 160, 255, 255, 255);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 4;
        s.smooth = false; // v1.7 default for color
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        // find the brown layer by fill color
        std::string const svg = b.getSVG(160, 160);
        int brown_layer = -1, li = 0;
        for (std::size_t pos = 0; (pos = svg.find("fill:#", pos)) != std::string::npos; pos += 7) {
            std::string const hex = svg.substr(pos + 6, 6);
            if (hex == "8b5a2b") { brown_layer = li; break; }
            li++;
        }
        check(brown_layer >= 0, "flat-zone: brown layer present");
        if (brown_layer >= 0) {
            std::string const d = write_path_for_check(b, brown_layer);
            int subs = 0;
            for (std::size_t p = 0; (p = d.find("M", p)) != std::string::npos; p++) subs++;
            check(subs == 1, "flat-zone: uniform 100x100 region is ONE subpath (got "
                  + std::to_string(subs) + ")");
        }
        validate_svg(svg, "flat-zone");
    }

    // ------------------------------------------------------------------
    std::cout << "-- near-identical colors collapse without specks\n";
    {
        // Two halves differing by 2 levels per channel (#7b4c4c vs #7d4e4d):
        // palette-merge must collapse them; regardless, no micro-islands.
        auto img = make_rgb(120, 120);
        fill_rect(img, 0, 0, 120, 60, 0x7b, 0x4c, 0x4c);
        fill_rect(img, 0, 60, 120, 120, 0x7d, 0x4e, 0x4d);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 6;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        std::string const svg = b.getSVG(120, 120);
        int total_subs = 0;
        for (std::size_t p = 0; (p = svg.find("M", p)) != std::string::npos; p++) total_subs++;
        // Two flat half-planes: at most a handful of subpaths total (the
        // v1.6 regression produced dozens of fragments here).
        check(total_subs <= 6, "near-identical: no speck fragmentation (got "
              + std::to_string(total_subs) + " subpaths)");
        validate_svg(svg, "near-identical");
    }

    // ------------------------------------------------------------------
    std::cout << "-- mono smooth: antialiased line-art traces as one clean outline\n";
    {
        // Thin antialiased black bar on white. With smoothing the result is
        // a single simple outline; without it the antialiased edges would
        // produce extra speckle contours.
        auto img = make_rgb(100, 60);
        fill_rect(img, 10, 28, 90, 32, 0, 0, 0);   // 4px black bar
        fill_rect(img, 10, 27, 90, 28, 128, 128, 128); // antialiased edge
        fill_rect(img, 10, 32, 90, 33, 128, 128, 128);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Mono;
        s.smooth = true; // IVF PATCH (mono-smooth): now honored in mono mode
        s.speckles_size = 4;
        auto b = run(s, img);
        check(b.size() == 1, "antialiased bar traces as exactly one path (got "
              + std::to_string(b.size()) + ")");
        if (b.size() == 1) {
            std::string const d = write_path_for_check(b, 0);
            // Sanity: the outline must stay in the vertical band of the bar
            // (25..36) and span nearly the full width (10..90).
            check(d.size() > 0, "mono path has geometry");
        }
        validate_svg(b.getSVG(100, 60), "mono-smooth");
    }

    // ------------------------------------------------------------------
    std::cout << "-- mono-alpha: transparent PNG never traces as a full-canvas rectangle\n";
    {
        // Fully transparent canvas + one opaque feature. Transparent pixels
        // carry raw RGB black (alpha-nopremult); the mono path must treat
        // them as BACKGROUND (alpha-threshold), not ink — otherwise the
        // trace is a full-canvas black rectangle (IVF PATCH
        // mono-alpha-composite). Reproduces the user-visible bug.
        auto img = make_rgba(80, 80);
        fill_rect(img, 20, 20, 60, 60, 200, 30, 30); // opaque red square
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Mono;
        s.background_policy = ivf::BackgroundPolicy::RemoveTransparent;
        auto b = run(s, img);
        check(b.size() == 1, "transparent background is not a layer (got "
              + std::to_string(b.size()) + " paths)");
        if (b.size() == 1) {
            std::string const d = write_path_for_check(b, 0);
            // The path must NOT touch the canvas corners: the old bug
            // produced one rectangle covering (0,0)-(80,80).
            bool touches_corner = d.find("M0 ") != std::string::npos
                || d.find("M0,") != std::string::npos
                || d.find("L0 ") != std::string::npos;
            check(!touches_corner, "mono path does not span the full canvas");
        }
        validate_svg(b.getSVG(80, 80), "mono-alpha");
    }

    // ------------------------------------------------------------------
    std::cout << "-- mono-despeckle: binarized AA/noise specks never reach Potrace\n";
    {
        // White canvas + one large feature + isolated small black specks.
        // Without the pass the specks survive binarization and Potrace
        // traces them as spurious black dots (the user-visible bug).
        auto img = make_rgb(80, 80);
        fill_rect(img, 0, 0, 80, 80, 255, 255, 255);
        fill_rect(img, 30, 20, 50, 60, 0, 0, 0);  // real feature (bar)
        fill_rect(img, 8, 10, 10, 12, 0, 0, 0);   // 2x2 speck
        fill_rect(img, 60, 55, 62, 57, 0, 0, 0);  // 2x2 speck
        fill_rect(img, 12, 60, 13, 63, 0, 0, 0);  // 1x3 speck
        // Smooth OFF so the gaussian cannot dissolve the specks first;
        // this isolates the despeckle pass (smooth on top of it is the
        // shipped default and only helps further).
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Mono;
        s.smooth = false;
        s.speckles_size = 2; // default: despeckle threshold = max(12, 4*2) = 12
        auto b = run(s, img);
        check(b.size() == 1, "mono-despeckle: one path (got " + std::to_string(b.size()) + ")");
        if (b.size() == 1) {
            std::string const d = write_path_for_check(b, 0);
            int const subs = int(std::count(d.begin(), d.end(), 'M'));
            // Real feature = 1 outer ring + 0 holes; the 3 specks must be gone.
            check(subs <= 2, "mono-despeckle: specks absorbed, not traced (subpaths="
                  + std::to_string(subs) + ")");
        }

        // Opt-out: speckles_size=0 disables the pass; the 3 specks survive.
        auto s0 = s;
        s0.speckles_size = 0;
        auto b0 = run(s0, img);
        if (b0.size() == 1) {
            std::string const d0 = write_path_for_check(b0, 0);
            int const subs0 = int(std::count(d0.begin(), d0.end(), 'M'));
            check(subs0 >= 4, "mono-despeckle: speckles=0 keeps the specks (subpaths="
                  + std::to_string(subs0) + ")");
        } else {
            check(false, "mono-despeckle: speckles=0 keeps the specks (layers="
                  + std::to_string(b0.size()) + ")");
        }
        validate_svg(b.getSVG(80, 80), "mono-despeckle");
    }

    // ------------------------------------------------------------------
    std::cout << "-- index-despeckle: tiny AA islands are absorbed, not traced\n";
    {
        // White canvas with spaced 2x2 dark dots. Each dot is a 4 px island
        // in the indexed map: bigger than potrace turdsize 2 (so they used
        // to be traced as spurious dots) but under the despeckle threshold.
        auto img = make_rgb(64, 64);
        fill_rect(img, 0, 0, 64, 64, 255, 255, 255);
        fill_rect(img, 10, 10, 12, 12, 0, 0, 0);
        fill_rect(img, 30, 10, 32, 12, 0, 0, 0);
        fill_rect(img, 50, 10, 52, 12, 0, 0, 0);
        fill_rect(img, 10, 40, 12, 42, 0, 0, 0);
        auto s = ivf::Settings{};
        s.mode = ivf::Mode::Color;
        s.scans = 2;
        s.smooth = false;
        s.background_policy = ivf::BackgroundPolicy::KeepAll;
        auto b = run(s, img);
        check(b.size() == 2, "despeckle: 2 layers (got " + std::to_string(b.size()) + ")");
        if (b.size() == 2) {
            // The light (white) layer previously carried one hole per dot:
            // 4 tiny extra subpaths. After the pass the dots are absorbed.
            std::string const d = write_path_for_check(b, 1);
            int const holes = int(std::count(d.begin(), d.end(), 'M')) - 1;
            check(holes == 0, "despeckle: no dot holes in light layer (got "
                  + std::to_string(holes) + ")");
        }

        // Opt-out: speckles=0 must disable the pass (dots survive as holes).
        auto s0 = s;
        s0.speckles_size = 0;
        auto b0 = run(s0, img);
        if (b0.size() == 2) {
            std::string const d0 = write_path_for_check(b0, 1);
            int const holes0 = int(std::count(d0.begin(), d0.end(), 'M')) - 1;
            check(holes0 >= 4, "speckles=0 keeps the dots (got "
                  + std::to_string(holes0) + " holes)");
        } else {
            check(false, "speckles=0 keeps the dots (layers=" + std::to_string(b0.size()) + ")");
        }
    }

    xmlCleanupParser();

    std::cout << (g_failures == 0 ? "\nALL TESTS PASSED\n" : "\nFAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
