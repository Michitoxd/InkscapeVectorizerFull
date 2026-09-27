// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tracer: high-level facade (v1.2 — simplified to two modes).
 *
 * Mono  : brightness cutoff through the extracted Potrace engine
 *         (equivalent to Inkscape's "Brightness cutoff" single scan).
 * Color : octree quantization + traceQuant through the extracted Potrace
 *         engine (equivalent to Inkscape's "Multiple scans: color").
 *
 * Preview: runs the SAME filter the real trace uses (no Potrace), so what
 * the user sees is exactly what will be vectorized.
 */

#ifndef IVF_TRACER_H
#define IVF_TRACER_H

#include <memory>
#include <string>
#include <cstdint>
#include "trace/trace.h"
#include "trace/potrace/inkscape-potrace.h"

namespace ivf {

enum class Mode
{
    Mono,   // single scan, brightness cutoff, one black layer
    Color,  // multiple scans, one layer per dominant color
};

/// What happens to background / transparent areas of the image.
enum class BackgroundPolicy
{
    KeepAll,                 // emit every layer (no removal)
    RemoveTransparent,       // if the image had alpha, leave transparent areas as holes
    RemoveDominantBorderColor, // remove the layer matching the dominant border color
    RemoveDetectedByColor,   // remove the layer matching --remove-bg-color
};

struct Settings
{
    Mode mode = Mode::Mono;

    // Mono mode
    double threshold = 0.45;      // brightness cutoff (0..1)

    // Color mode
    int scans = 8;                // number of color layers (2..256)
    bool stack = true;            // stack scans (else tile)
    bool smooth = true;           // gaussian smoothing before quantization

    // Common potrace path options
    bool invert = false;
    bool optimize = true;         // curve optimization (opticurve)
    double opt_tolerance = 0.2;   // optimization tolerance
    double smooth_corners = 1.0;  // corner smoothing (alphamax, 0..1.334)
    int speckles_size = 2;        // speckle removal (turdsize, 0 = off)

    // Background handling (see docs/FIX_FONDO.md)
    BackgroundPolicy background_policy = BackgroundPolicy::RemoveTransparent;
    uint32_t remove_bg_color = 0xffffff; // used by RemoveDetectedByColor (#RRGGBB)
    double background_area_threshold = 0.95; // min area fraction to treat a layer as background
};

/**
 * Facade over the extracted Inkscape Potrace tracing engine.
 */
class Tracer
{
public:
    explicit Tracer(Settings settings);

    /// Trace a pixbuf. progress may be null.
    Inkscape::Trace::SvgBuilder trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf,
                                      Inkscape::Async::Progress<double> *progress = nullptr);

    /// Preview: the exact filter the trace will use (brightness map for Mono,
    /// quantized palette image for Color). Never runs Potrace.
    Glib::RefPtr<Gdk::Pixbuf> preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);

    /// True if the source pixbuf (or the last traced one) has an alpha channel.
    static bool has_alpha(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf);

    Settings const &settings() const { return _settings; }

private:
    std::unique_ptr<Inkscape::Trace::Potrace::PotraceTracingEngine> make_engine() const;

    Settings _settings;
};

/// Parse '#RRGGBB' or 'RRGGBB' into 0x00RRGGBB. Exposed for the CLI.
bool parse_hex_color_public(std::string const &s, uint32_t &out);

} // namespace ivf

#endif // IVF_TRACER_H
