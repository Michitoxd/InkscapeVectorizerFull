// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * trace-bitmap: standalone CLI for the extracted Inkscape Trace Bitmap core.
 * v1.2 — two modes (mono | color), background policies, --preview-out.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glibmm/init.h>
#include <gdkmm/wrap_init.h>
#include <gdkmm/pixbuf.h>

#include "core/imageio.h"
#include "core/tracer.h"
#include "core/async/progress.h"

namespace {

void usage(char const *prog)
{
    std::cout
        << "Usage: " << prog << " [options] <input-image> <output.svg>\n"
        << "\n"
        << "Vectorize a bitmap to SVG using the tracing core extracted from\n"
        << "Inkscape " << "1.4.4" << " (Trace Bitmap feature).\n"
        << "\n"
        << "Modes:\n"
        << "  -m, --mode MODE        mono | color  (default: mono)\n"
        << "                         mono  = brightness cutoff, single black layer\n"
        << "                         color = multiple scans, one layer per color\n"
        << "\n"
        << "Color mode:\n"
        << "  -s, --scans N          number of colors/layers, 2..256 (default 8)\n"
        << "\n"
        << "Presets (docs/FIDELIDAD.md):\n"
        << "      --preset NAME      line-art | logo | photo | high\n"
        << "                         applies a bundle of settings first; individual\n"
        << "                         flags after --preset override it\n"
        << "      --smooth           gaussian blur before quantizing (default)\n"
        << "      --no-smooth        disable gaussian blur\n"
        << "      --stack            stack layers (default)\n"
        << "      --tile             tile layers instead of stacking\n"
        << "\n"
        << "Mono mode:\n"
        << "  -t, --threshold N      brightness cutoff, 0..1 (default 0.45)\n"
        << "      --invert           invert the bitmap before tracing\n"
        << "\n"
        << "Path options:\n"
        << "      --optimize         optimize curves (default)\n"
        << "      --no-optimize      disable curve optimization\n"
        << "      --opt-tolerance N  optimization tolerance (default 0.2)\n"
        << "      --smooth-corners N corner smoothing, 0..1.334 (default 1.0)\n"
        << "      --speckles N       speckle size in px, 0=off (default 2)\n"
        << "\n"
        << "Background handling (docs/FIX_FONDO.md):\n"
        << "      --background-policy POLICY\n"
        << "                         keep-all | remove-transparent (default) |\n"
        << "                         remove-border | remove-color\n"
        << "      --remove-bg-color '#RRGGBB'  color for remove-color policy\n"
        << "      --background-area-threshold F\n"
        << "                         min area fraction to treat as background\n"
        << "                         (default 0.95; protects white content)\n"
        << "\n"
        << "Output:\n"
        << "      --preview-out FILE also write the filter preview as PNG\n"
        << "  -q, --quiet            suppress progress output\n"
        << "  -h, --help             show this help\n"
        << "  -V, --version          show version\n";
}

struct ProgressReporter final
    : public Inkscape::Async::Progress<double>
{
    explicit ProgressReporter(bool verbose)
        : _verbose(verbose) {}

    bool _keepgoing() const override { return true; }

    bool _report(double const &v) override
    {
        if (_verbose) {
            int pct = int(v * 100 + 0.5);
            if (pct != _last) {
                _last = pct;
                std::cout << "\rtracing: " << pct << "%" << std::flush;
                if (pct == 100) {
                    std::cout << "\n";
                }
            }
        }
        return true;
    }

    bool _verbose;
    int _last = -1;
};

double get_double(std::string const &value, char const *what)
{
    try {
        size_t pos = 0;
        double v = std::stod(value, &pos);
        if (pos != value.size()) {
            throw std::invalid_argument("trailing chars");
        }
        return v;
    } catch (std::exception const &) {
        throw std::runtime_error(std::string("invalid number for ") + what + ": " + value);
    }
}

int get_int(std::string const &value, char const *what)
{
    try {
        size_t pos = 0;
        int v = std::stoi(value, &pos);
        if (pos != value.size()) {
            throw std::invalid_argument("trailing chars");
        }
        return v;
    } catch (std::exception const &) {
        throw std::runtime_error(std::string("invalid integer for ") + what + ": " + value);
    }
}

/*
 * Quality presets (docs/FIDELIDAD.md). Applied before flag parsing so any
 * explicit flag after --preset overrides the preset value.
 */
void apply_preset(std::string const &name, ivf::Settings &s)
{
    if (name == "line-art") {
        s.mode = ivf::Mode::Mono;
        s.smooth = true;          // IVF PATCH (mono-smooth): calms antialiasing
        s.speckles_size = 4;      // kill antialiasing motes
        s.smooth_corners = 0.7;
        s.opt_tolerance = 0.1;
    } else if (name == "logo") {
        s.mode = ivf::Mode::Color;
        s.scans = 8;
        s.stack = true;
        s.smooth = true;
        s.speckles_size = 4;
        s.smooth_corners = 0.5;
        s.opt_tolerance = 0.05;
    } else if (name == "photo") {
        s.mode = ivf::Mode::Color;
        s.scans = 32;
        s.stack = true;
        s.smooth = false;         // preserve detail
        s.speckles_size = 1;
        s.smooth_corners = 1.0;
        s.opt_tolerance = 0.1;
    } else if (name == "high") {
        s.mode = ivf::Mode::Color;
        s.scans = 48;
        s.stack = true;
        s.smooth = false;
        s.speckles_size = 0;
        s.smooth_corners = 0.8;
        s.opt_tolerance = 0.02;
    } else {
        throw std::runtime_error("unknown preset: " + name +
                                 " (use line-art|logo|photo|high)");
    }
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv, argv + argc);

    ivf::Settings settings;
    std::string input, output, preview_out;
    bool quiet = false;
    std::string policy_str = "auto";

    try {
        // Pass 1: apply --preset first so later flags can override it.
        for (std::size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--preset") {
                if (++i >= args.size()) {
                    throw std::runtime_error("missing value for --preset");
                }
                apply_preset(args[i], settings);
            }
        }

        for (std::size_t i = 1; i < args.size(); i++) {
            std::string const &a = args[i];

            auto next = [&](char const *what) -> std::string {
                if (++i >= args.size()) {
                    throw std::runtime_error(std::string("missing value for ") + what);
                }
                return args[i];
            };

            if (a == "--preset") {
                next("--preset"); // value consumed; preset was applied in pass 1
            } else if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            } else if (a == "-V" || a == "--version") {
                std::cout << "trace-bitmap (InkscapeVectorizerFull) 1.7\n"
                          << "Tracing core extracted from Inkscape INKSCAPE_1_4_4\n";
                return 0;
            } else if (a == "-m" || a == "--mode") {
                std::string m = next("--mode");
                if (m == "mono") settings.mode = ivf::Mode::Mono;
                else if (m == "color") {
                    settings.mode = ivf::Mode::Color;
                    // IVF PATCH (smooth-off-color) [v1.7]: Inkscape's "Smooth"
                    // checkbox is OFF by default in multicolor mode, and the
                    // measured fidelity agrees: the 5×5 gaussian turns every
                    // hard edge into an 8-9-step color ramp that eats palette
                    // slots and leaves streak residue in flat regions
                    // (docs/FIDELIDAD_v2.md). Mono keeps smooth=true: there
                    // the ramp falls to a single binarization threshold.
                    settings.smooth = false;
                    // IVF PATCH (scans-16) [v1.7]: default 8 → 16, matching
                    // Inkscape's typical multicolor usage; measurably better
                    // fidelity on multi-color inputs, modest node cost.
                    settings.scans = 16;
                }
                else throw std::runtime_error("unknown mode: " + m + " (use mono|color)");
            } else if (a == "-t" || a == "--threshold") {
                settings.threshold = get_double(next("--threshold"), "--threshold");
            } else if (a == "--invert") {
                settings.invert = true;
            } else if (a == "-s" || a == "--scans") {
                settings.scans = get_int(next("--scans"), "--scans");
            } else if (a == "--smooth") {
                settings.smooth = true;
            } else if (a == "--no-smooth") {
                settings.smooth = false;
            } else if (a == "--stack") {
                settings.stack = true;
            } else if (a == "--tile") {
                settings.stack = false;
            } else if (a == "--optimize") {
                settings.optimize = true;
            } else if (a == "--no-optimize") {
                settings.optimize = false;
            } else if (a == "--opt-tolerance") {
                settings.opt_tolerance = get_double(next("--opt-tolerance"), "--opt-tolerance");
            } else if (a == "--smooth-corners") {
                settings.smooth_corners = get_double(next("--smooth-corners"), "--smooth-corners");
            } else if (a == "--speckles") {
                settings.speckles_size = get_int(next("--speckles"), "--speckles");
            } else if (a == "--background-policy") {
                policy_str = next("--background-policy");
            } else if (a == "--remove-bg-color") {
                std::string c = next("--remove-bg-color");
                uint32_t v = 0;
                if (!ivf::parse_hex_color_public(c, v)) {
                    throw std::runtime_error("invalid color (expected #RRGGBB): " + c);
                }
                settings.remove_bg_color = v;
            } else if (a == "--background-area-threshold") {
                settings.background_area_threshold =
                    get_double(next("--background-area-threshold"), "--background-area-threshold");
            } else if (a == "--preview-out") {
                preview_out = next("--preview-out");
            } else if (a == "-q" || a == "--quiet") {
                quiet = true;
            } else if (!a.empty() && a[0] == '-' && a != "-") {
                throw std::runtime_error("unknown option: " + a);
            } else if (input.empty()) {
                input = a;
            } else if (output.empty()) {
                output = a;
            } else {
                throw std::runtime_error("unexpected argument: " + a);
            }
        }

        // Resolve the "auto" default policy after loading the image.
        if (policy_str == "keep-all") {
            settings.background_policy = ivf::BackgroundPolicy::KeepAll;
        } else if (policy_str == "remove-transparent") {
            settings.background_policy = ivf::BackgroundPolicy::RemoveTransparent;
        } else if (policy_str == "remove-border") {
            settings.background_policy = ivf::BackgroundPolicy::RemoveDominantBorderColor;
        } else if (policy_str == "remove-color") {
            settings.background_policy = ivf::BackgroundPolicy::RemoveDetectedByColor;
        } else if (policy_str == "auto") {
            settings.background_policy = ivf::BackgroundPolicy::RemoveTransparent; // falls back internally
        } else {
            throw std::runtime_error("unknown --background-policy: " + policy_str);
        }
    } catch (std::exception const &e) {
        std::cerr << "error: " << e.what() << "\n";
        std::cerr << "try '" << argv[0] << " --help'\n";
        return 2;
    }

    if (input.empty() || output.empty()) {
        usage(argv[0]);
        return 2;
    }

    Glib::init();
    Gdk::wrap_init();

    try {
        auto pixbuf = ivf::load_image(input);
        if (!pixbuf) {
            std::cerr << "error: could not load image: " << input << "\n";
            return 1;
        }

        if (!quiet) {
            std::cout << "input: " << input << " ("
                      << pixbuf->get_width() << "x" << pixbuf->get_height()
                      << (ivf::has_alpha(pixbuf) ? ", alpha)" : ")") << "\n";
        }

        ivf::Tracer tracer(settings);
        ProgressReporter reporter(!quiet);
        auto builder = tracer.trace(pixbuf, &reporter);

        if (builder.size() == 0) {
            std::cerr << "warning: tracing produced no paths "
                         "(image may be empty, or threshold may exclude everything)\n";
        }

        auto svg = builder.getSVG(pixbuf->get_width(), pixbuf->get_height());
        std::ofstream out(output, std::ios::binary);
        if (!out) {
            std::cerr << "error: cannot write output file: " << output << "\n";
            return 1;
        }
        out << svg;
        out.close();

        if (!preview_out.empty()) {
            auto pv = tracer.preview(pixbuf);
            if (pv) {
                pv->save(preview_out, "png");
                if (!quiet) std::cout << "preview: " << preview_out << "\n";
            }
        }

        if (!quiet) {
            std::cout << "output: " << output << " ("
                      << builder.size() << " path(s), "
                      << builder.nodeCount() << " node(s))\n";
        }
        return 0;

    } catch (Glib::Error const &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (std::exception const &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
