// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tracer facade implementation (v1.2 — two modes, honest preview,
 * background policies). See tracer.h.
 *
 * Engine wiring mirrors TraceDialogImpl::getTraceData() from Inkscape's
 * src/ui/dialog/tracedialog.cpp (INKSCAPE_1_4_4) for the potrace modes.
 */

#include "tracer.h"

#include "trace/trace.h"
#include "trace/potrace/inkscape-potrace.h"
#include "trace/imagemap-gdk.h"
#include "trace/imagemap.h"
#include "async/progress.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <array>

namespace ivf {

using Inkscape::Trace::Potrace::PotraceTracingEngine;
using Inkscape::Trace::Potrace::TraceType;

static TraceType potrace_type(Mode mode)
{
    return mode == Mode::Color ? TraceType::QUANT_COLOR : TraceType::BRIGHTNESS;
}

Tracer::Tracer(Settings settings)
    : _settings(std::move(settings)) {}

std::unique_ptr<PotraceTracingEngine> Tracer::make_engine() const
{
    // Wiring identical to setup_potrace() in tracedialog.cpp (INKSCAPE_1_4_4).
    auto eng = std::make_unique<PotraceTracingEngine>(
        potrace_type(_settings.mode),
        _settings.invert,
        /*quantizationNrColors*/ _settings.scans,
        /*brightnessThreshold*/ _settings.threshold,
        /*brightnessFloor*/ 0.0,
        /*cannyHighThreshold*/ 0.65, // unused by the two remaining modes
        /*multiScanNrColors*/ _settings.scans,
        /*multiScanStack*/ _settings.stack,
        /*multiScanSmooth*/ _settings.smooth,
        /*multiScanRemoveBackground*/ false); // handled by BackgroundPolicy here

    eng->setOptiCurve(_settings.optimize);
    eng->setOptTolerance(_settings.opt_tolerance);
    eng->setAlphaMax(_settings.smooth_corners);
    eng->setTurdSize(_settings.speckles_size);
    return eng;
}

bool Tracer::has_alpha(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    return pixbuf && pixbuf->get_has_alpha();
}

namespace {

class ProgressForwarder final
    : public Inkscape::Async::Progress<double>
{
public:
    explicit ProgressForwarder(Inkscape::Async::Progress<double> *target)
        : _target(target ? target : &_null) {}

    bool _keepgoing() const override { return _target->keepgoing(); }
    bool _report(double const &v) override { return _target->report(v); }

private:
    Inkscape::Async::ProgressAlways<double> _null;
    Inkscape::Async::Progress<double> *_target;
};

// ---------------------------------------------------------------------------
// Background policy helpers (docs/FIX_FONDO.md)
// ---------------------------------------------------------------------------

struct Rgb
{
    int r, g, b;
};

/// Dominant color on the image border (first/last rows and columns).
/// Returns true and fills `out` when a single color covers >= `frac` of the
/// border pixels.
/// IVF PATCH (border-bucket): colors are grouped in 32-levels-per-channel
/// buckets instead of exact equality. Real borders carry JPEG/AA noise
/// (e.g. #f4f4f4..#fafafa): exact grouping split the vote so no color
/// reached `frac` and the background went undetected. The bucket's mean
/// color is the background candidate; the final layer match is perceptual
/// (rgb_delta_e), which absorbs the remaining intra-bucket spread.
bool dominant_border_color(Glib::RefPtr<Gdk::Pixbuf> const &pb, double frac, Rgb &out)
{
    if (!pb) return false;
    int const w = pb->get_width();
    int const h = pb->get_height();
    if (w <= 0 || h <= 0) return false;

    int const rowstride = pb->get_rowstride();
    int const nch = pb->get_n_channels();
    auto const *data = pb->get_pixels();

    auto px = [&](int x, int y) -> Rgb {
        auto const *p = data + y * rowstride + x * nch;
        return {p[0], p[1], p[2]};
    };

    struct Acc { long sr = 0, sg = 0, sb = 0, n = 0; };
    std::map<uint32_t, Acc> buckets;
    long total = 0;
    // 16-levels-per-channel buckets: JPEG noise of ±4 levels around a flat
    // border must land in ONE bucket, or the vote splinters below `frac`.
    auto const bucket = [](int v) { return v >> 4; };
    auto add = [&](Rgb c) {
        uint32_t key = (uint32_t(bucket(c.r)) << 8) | (uint32_t(bucket(c.g)) << 4) | uint32_t(bucket(c.b));
        Acc &a = buckets[key];
        a.sr += c.r; a.sg += c.g; a.sb += c.b; a.n++;
        total++;
    };
    // IVF PATCH (corner-count): count each border pixel exactly once —
    // the previous loop counted the 4 corner pixels in both passes,
    // biasing the dominant-color vote toward corner colors on small images.
    for (int x = 1; x < w - 1; x++) { add(px(x, 0)); add(px(x, h - 1)); }
    for (int y = 0; y < h; y++) { add(px(0, y)); add(px(w - 1, y)); }

    long best = 0; Rgb bestc{0, 0, 0};
    for (auto const &[key, a] : buckets) {
        if (a.n > best) {
            best = a.n;
            bestc = {int(a.sr / a.n), int(a.sg / a.n), int(a.sb / a.n)};
        }
    }
    if (total <= 0 || double(best) / total < frac) return false;
    out = bestc;
    return true;
}

/// Fraction of source pixels whose color is within `tol` per channel of
/// `bg`. IVF PATCH (bg-area): the background-candidate test must measure the
/// color's real coverage in the source image, not traced layer geometry —
/// in stack mode every layer's traced geometry covers ~100% of the canvas,
/// which made any color match look like a background. Fully transparent
/// pixels (RGBA) are excluded from the visible area.
double bg_color_area_fraction(Glib::RefPtr<Gdk::Pixbuf> const &pb, Rgb bg, int tol = 24)
{
    if (!pb) return 0.0;
    int const w = pb->get_width();
    int const h = pb->get_height();
    if (w <= 0 || h <= 0) return 0.0;

    int const rowstride = pb->get_rowstride();
    int const nch = pb->get_n_channels();
    auto const *data = pb->get_pixels();

    long hit = 0, total = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            auto const *p = data + y * rowstride + x * nch;
            if (nch == 4 && p[3] == 0) continue; // invisible: not background
            ++total;
            if (std::abs(p[0] - bg.r) <= tol && std::abs(p[1] - bg.g) <= tol && std::abs(p[2] - bg.b) <= tol) {
                ++hit;
            }
        }
    }
    return total > 0 ? double(hit) / double(total) : 0.0;
}

/// Squared distance in RGB space.
int rgb_dist2(Rgb a, Rgb b)
{
    int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

// IVF PATCH (bg-lab): approximate CIE-Lab ΔE for the background/tolerance
// match (docs/RECOMENDACIONES.md R2). RGB Euclidean distance treats all
// channels equally, so near-white tones (#fdfdfd vs #ffffff — trivially the
// same to the eye) can escape an RGB tolerance while saturated colors get
// over-matched. Converting through sRGB→XYZ→Lab (D65, gamma 2.2 approximation
// of the sRGB curve — plenty for a 10-ΔE threshold) and comparing ΔE76 makes
// the tolerance perceptual. For near-grays (the actual use case: background
// detection) this is within ~1 ΔE of the exact sRGB transfer.
double rgb_delta_e(Rgb a, Rgb b)
{
    auto srgb_to_lab = [](int c) -> double {
        double v = c / 255.0;
        v = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        return v;
    };
    auto to_lab = [&](Rgb c) {
        double r = srgb_to_lab(c.r), g = srgb_to_lab(c.g), b = srgb_to_lab(c.b);
        // linear sRGB → XYZ (D65)
        double X = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
        double Y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
        double Z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
        auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
        double fx = f(X / 0.95047), fy = f(Y / 1.0), fz = f(Z / 1.08883);
        return std::array<double, 3>{116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
    };
    auto A = to_lab(a), B = to_lab(b);
    double dL = A[0] - B[0], da = A[1] - B[1], db = A[2] - B[2];
    return std::sqrt(dL * dL + da * da + db * db);
}

/// Parse '#RRGGBB' or 'RRGGBB'.
bool parse_hex_color(std::string const &s, uint32_t &out)
{
    std::string t = s;
    if (!t.empty() && t[0] == '#') t = t.substr(1);
    if (t.size() != 6) return false;
    uint32_t v = 0;
    for (char c : t) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (c - '0');
        else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

} // namespace

// exposed for CLI parsing
bool parse_hex_color_public(std::string const &s, uint32_t &out) { return parse_hex_color(s, out); }

Inkscape::Trace::SvgBuilder Tracer::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf,
                                          Inkscape::Async::Progress<double> *progress)
{
    Inkscape::Trace::SvgBuilder builder;
    if (!pixbuf) {
        return builder;
    }

    ProgressForwarder forwarder(progress);
    auto engine = make_engine();
    auto result = engine->trace(pixbuf, forwarder);

    // IVF PATCH (invisible-guard): a fully-transparent image still reaches
    // the mono-style brightness path inside the engines via its (invisible)
    // RGB values, which can emit one dark layer covering the canvas even
    // though nothing is visible. Guard: with an alpha channel and zero
    // visible pixels, drop everything.
    if (pixbuf->get_has_alpha()) {
        bool any_visible = false;
        int const rs = pixbuf->get_rowstride();
        int const nch = pixbuf->get_n_channels();
        auto const *d = pixbuf->get_pixels();
        for (int y = 0; y < pixbuf->get_height() && !any_visible; y++) {
            for (int x = 0; x < pixbuf->get_width(); x++) {
                if (d[y * rs + x * nch + 3] != 0) { any_visible = true; break; }
            }
        }
        if (!any_visible) {
            return builder; // empty SVG: nothing visible
        }
    }

    // Resolve the effective policy.
    BackgroundPolicy policy = _settings.background_policy;
    if (policy == BackgroundPolicy::RemoveTransparent && !pixbuf->get_has_alpha()) {
        policy = BackgroundPolicy::RemoveDominantBorderColor;
    }

    // Determine the background color to remove, if any.
    bool have_bg = false;
    Rgb bg{255, 255, 255};
    switch (policy) {
        case BackgroundPolicy::KeepAll:
            break;
        case BackgroundPolicy::RemoveTransparent:
            // Transparent areas simply never reach the engines anymore
            // (alpha-aware converters); no layer to remove here.
            break;
        case BackgroundPolicy::RemoveDominantBorderColor:
            have_bg = dominant_border_color(pixbuf, 0.60, bg);
            break;
        case BackgroundPolicy::RemoveDetectedByColor:
            bg = {int(_settings.remove_bg_color >> 16 & 0xff),
                  int(_settings.remove_bg_color >> 8 & 0xff),
                  int(_settings.remove_bg_color & 0xff)};
            have_bg = true;
            break;
    }

    // IVF PATCH (bg-area): area-threshold semantics differ per policy.
    //  - RemoveDetectedByColor (explicit user color): the color is only
    //    treated as background if it really covers >= background_area_threshold
    //    of the visible pixels (protects legitimate interior content).
    //  - RemoveDominantBorderColor: border dominance (>=60% of border pixels)
    //    is itself the evidence; interior pockets of the same color survive
    //    visually as transparent holes (rendered on the page background).
    bool const apply_area_guard = (policy == BackgroundPolicy::RemoveDetectedByColor);
    double const bg_area = have_bg ? bg_color_area_fraction(pixbuf, bg, 24) : 0.0;

    for (auto &item : result) {
        if (have_bg && (!apply_area_guard || bg_area >= _settings.background_area_threshold)) {
            // Extract the fill color from the style string ("fill:#rrggbb...").
            auto pos = item.style.find("#");
            if (pos != std::string::npos && pos + 6 <= item.style.size()) {
                uint32_t c = 0;
                if (parse_hex_color(item.style.substr(pos, 7), c)) {
                    Rgb layer{int(c >> 16 & 0xff), int(c >> 8 & 0xff), int(c & 0xff)};
                    // Closest palette layer only.
                    // IVF PATCH (bg-lab, was bg-tolerance): anti-aliased/JPEG
                    // backgrounds produce near-bg palette entries (#fdfdfd,
                    // #f6f6f6) that RGB distance misjudges; the match is now
                    // perceptual ΔE in CIE-Lab, threshold 10 (≈ the old
                    // ±24/channel for near-grays, but consistent for
                    // saturated backgrounds too).
                    if (rgb_delta_e(layer, bg) <= 10.0) {
                        continue; // drop the background layer
                    }
                }
            }
        }
        builder.addPath(std::move(item.style), std::move(item.path));
    }
    return builder;
}

Glib::RefPtr<Gdk::Pixbuf> Tracer::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    if (!pixbuf) {
        return {};
    }

    // Honest preview: run exactly the filter the real trace uses.
    // Both Mono and Color go through the engine's own preview(): upstream
    // PotraceTracingEngine::preview() calls filter() for BRIGHTNESS and
    // filterIndexed() for QUANT_COLOR — the same filters the trace uses,
    // so the preview can never drift from the trace logic.
    auto engine = make_engine();
    return engine->preview(pixbuf);
}

} // namespace ivf
