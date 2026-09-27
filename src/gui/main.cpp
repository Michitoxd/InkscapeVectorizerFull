// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * trace-bitmap-gui: minimal GTK3 GUI for the extracted Inkscape Trace
 * Bitmap core (v1.3).
 *
 * New code (not extracted from Inkscape). Layout and parameter semantics
 * mirror Inkscape's Trace Bitmap dialog, but built in code (no Glade) and
 * decoupled from Inkscape's widget/DialogBase infrastructure.
 *
 * The preview uses ivf::Tracer::preview(), which runs the EXACT filter the
 * real trace uses (brightness map for Mono, gaussian+octree palette for
 * Color) and never runs Potrace — so the preview never lies.
 *
 * Tracing runs on a worker thread; the result is marshalled back to the
 * GTK main loop via Glib::Dispatcher. Progress is reported through
 * std::atomic (thread-safe, no direct widget access from the worker).
 */

#include <atomic>
#include <fstream>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "core/imageio.h"
#include "core/tracer.h"
#include "core/async/progress.h"

namespace {

// ---------------------------------------------------------------------------
// Worker: trace on a background thread, keep everything the UI thread reads
// behind atomics / dispatcher.
// ---------------------------------------------------------------------------
class TraceWorker final : public Inkscape::Async::Progress<double>
{
public:
    TraceWorker(ivf::Settings settings, Glib::RefPtr<Gdk::Pixbuf> pixbuf,
                Glib::Dispatcher &done, Glib::Dispatcher &failed,
                std::atomic<int> &progress_pct)
        : _settings(std::move(settings))
        , _pixbuf(std::move(pixbuf))
        , _done(done)
        , _failed(failed)
        , _pct(progress_pct)
    {}

    void run()
    {
        try {
            ivf::Tracer tracer(_settings);
            auto builder = tracer.trace(_pixbuf, this);
            _result = std::make_shared<Inkscape::Trace::SvgBuilder>(std::move(builder));
            _error.clear();
        } catch (Inkscape::Async::CancelledException const &) {
            _result.reset();
            _error.clear();
        } catch (Glib::Error const &e) {
            _result.reset();
            _error = e.what();
        } catch (std::exception const &e) {
            _result.reset();
            _error = e.what();
        }
        _finished = true;
        // Deliver the outcome to the UI thread (whichever applies).
        if (_result) {
            _done.emit();
        } else {
            _failed.emit();
        }
    }

    // Async::Progress (called from the potrace callback on our worker thread)
    bool _keepgoing() const override { return !_cancelled.load(std::memory_order_relaxed); }
    bool _report(double const &v) override
    {
        _pct.store(int(v * 100 + 0.5), std::memory_order_relaxed);
        return true;
    }

    std::shared_ptr<Inkscape::Trace::SvgBuilder> takeResult() { return _result; }
    std::string const &error() const { return _error; }
    void cancel() { _cancelled.store(true, std::memory_order_relaxed); }
    bool finished() const { return _finished.load(std::memory_order_relaxed); }

private:
    ivf::Settings _settings;
    Glib::RefPtr<Gdk::Pixbuf> _pixbuf;
    Glib::Dispatcher &_done;
    Glib::Dispatcher &_failed;
    std::atomic<int> &_pct;

    std::shared_ptr<Inkscape::Trace::SvgBuilder> _result;
    std::string _error;
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _finished{false};
};

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
class TraceWindow : public Gtk::Window
{
public:
    TraceWindow()
    {
        set_title("InkscapeVectorizerFull — Trace Bitmap");
        set_default_size(980, 640);
        set_border_width(8);

        buildWidgets();
        wireSignals();

        // Dispatchers: worker thread -> main loop
        _done.connect(sigc::mem_fun(*this, &TraceWindow::onTraceDone));
        _failed.connect(sigc::mem_fun(*this, &TraceWindow::onTraceFailed));

        // Progress polling (cheap; avoids a dispatcher per percent)
        _poll = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &TraceWindow::onPollProgress), 80);

        updateSensitivity();
        show_all_children();
    }

    // IVF PATCH (gui-join): joining a joinable Glib::Thread after it is
    // destroyed aborts the process. Cancel the worker and join before the
    // window (and its dispatchers) go away, so closing the app mid-trace
    // cannot crash at shutdown.
    ~TraceWindow() override
    {
        _poll.disconnect();
        if (_worker) {
            _worker->cancel();
        }
        if (_thread) {
            _thread->join();
            _thread = nullptr;
        }
        _worker.reset();
    }

private:
    // ------------------------------------------------------------------
    void buildWidgets()
    {
        // Left: preview area. Right: controls.
        _image.set_vexpand(true);
        _image.set_halign(Gtk::ALIGN_CENTER);
        _image.set_valign(Gtk::ALIGN_CENTER);
        _image.set("No image loaded");

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->add(_image);
        scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

        _frame.set_label(" Preview (same filter as the trace) ");
        _frame.add(*scroll);
        _frame.set_size_request(520, -1);

        buildControls();

        _paned.pack1(_frame, true, false);
        _paned.pack2(_controls, false, false);
        _paned.set_position(560);

        add(_paned);
    }

    void buildControls()
    {
        _controls.set_orientation(Gtk::ORIENTATION_VERTICAL);
        _controls.set_spacing(6);

        // --- file row -----------------------------------------------------
        auto file_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        _btn_open.set_label("Open image…");
        file_box->pack_start(_btn_open, Gtk::PACK_SHRINK);
        _file_label.set_ellipsize(Pango::ELLIPSIZE_START);
        _file_label.set_hexpand(true);
        file_box->pack_start(_file_label, Gtk::PACK_EXPAND_WIDGET);
        _controls.pack_start(*file_box, Gtk::PACK_SHRINK);

        // --- mode ----------------------------------------------------------
        _mode_mono.set_label("Monochrome (brightness cutoff)");
        _mode_color.set_label("Color (multiple scans)");
        _mode_mono.join_group(_mode_color);
        _mode_mono.set_active(true);
        auto mode_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        mode_box->pack_start(_mode_mono, Gtk::PACK_SHRINK);
        mode_box->pack_start(_mode_color, Gtk::PACK_SHRINK);

        attach(_mode_frame, "Mode", *mode_box);

        // --- mono params -----------------------------------------------------
        _threshold.set_range(0.0, 1.0);
        _threshold.set_increments(0.01, 0.05);
        _threshold.set_value(0.45);
        _threshold.set_digits(2);
        attach(_threshold_frame, "Brightness threshold (0–1)", _threshold);

        _invert.set_label("Invert image");
        _controls.pack_start(_invert, Gtk::PACK_SHRINK);

        // --- color params ----------------------------------------------------
        _scans.set_range(2, 256);
        _scans.set_increments(1, 8);
        _scans.set_value(16);
        attach(_scans_frame, "Number of colors (2–256)", _scans);

        _stack.set_label("Stack layers (else tile)");
        _stack.set_active(true);
        _controls.pack_start(_stack, Gtk::PACK_SHRINK);

        _smooth.set_label("Gaussian smoothing before quantizing");
        _smooth.set_active(true);
        _controls.pack_start(_smooth, Gtk::PACK_SHRINK);
        // IVF PATCH (smooth-off-color) [v1.7]: when Color mode is selected,
        // the checkbox mirrors the new CLI default (OFF), matching Inkscape.
        _mode_color.signal_toggled().connect([this] {
            _smooth.set_active(!_mode_color.get_active());
        });

        // --- path options ------------------------------------------------
        _optimize.set_label("Optimize curves");
        _optimize.set_active(true);
        _controls.pack_start(_optimize, Gtk::PACK_SHRINK);

        _opt_tolerance.set_range(0.0, 2.0);
        _opt_tolerance.set_increments(0.01, 0.1);
        _opt_tolerance.set_value(0.2);
        _opt_tolerance.set_digits(3);
        attach(_opttol_frame, "Optimization tolerance", _opt_tolerance);

        _smooth_corners.set_range(0.0, 1.334);
        _smooth_corners.set_increments(0.01, 0.1);
        _smooth_corners.set_value(1.0);
        _smooth_corners.set_digits(3);
        attach(_alphamax_frame, "Corner smoothing (0–1.334)", _smooth_corners);

        _speckles.set_range(0, 100);
        _speckles.set_increments(1, 5);
        _speckles.set_value(2);
        attach(_speckles_frame, "Speckle size (px, 0=off)", _speckles);

        // --- background policy -------------------------------------------
        _policy.append("auto (transparent → holes; else border color)");
        _policy.append("keep all layers");
        _policy.append("remove dominant border color");
        _policy.append("remove specific color…");
        _policy.set_active(0);
        attach(_policy_frame, "Background handling", _policy);

        _bg_color.set_text("#ffffff");
        attach(_bgcolor_frame, "Background color", _bg_color);

        _area_threshold.set_range(0.0, 1.0);
        _area_threshold.set_increments(0.01, 0.05);
        _area_threshold.set_value(0.95);
        _area_threshold.set_digits(2);
        attach(_areathr_frame, "Background area threshold", _area_threshold);

        // --- preview button ----------------------------------------------
        _btn_preview.set_label("Update preview");
        _btn_preview.set_sensitive(false);
        _controls.pack_start(_btn_preview, Gtk::PACK_SHRINK);

        // --- action row ---------------------------------------------------
        auto action_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        _btn_trace.set_label("Trace");
        _btn_trace.get_style_context()->add_class("suggested-action");
        _btn_trace.set_sensitive(false);
        _btn_export.set_label("Export SVG…");
        _btn_export.set_sensitive(false);
        _btn_cancel.set_label("Cancel");
        _btn_cancel.set_sensitive(false);
        action_box->pack_start(_btn_trace, Gtk::PACK_SHRINK);
        action_box->pack_start(_btn_cancel, Gtk::PACK_SHRINK);
        action_box->pack_start(_btn_export, Gtk::PACK_SHRINK);
        _controls.pack_start(*action_box, Gtk::PACK_SHRINK);

        // --- progress + status -------------------------------------------
        _progress.set_show_text(true);
        _controls.pack_start(_progress, Gtk::PACK_SHRINK);
        _status.set_xalign(0.0);
        _status.set_ellipsize(Pango::ELLIPSIZE_END);
        _controls.pack_start(_status, Gtk::PACK_SHRINK);
    }

    void attach(Gtk::Frame *&frame, Glib::ustring const &label, Gtk::Widget &child)
    {
        frame = Gtk::make_managed<Gtk::Frame>(label);
        frame->add(child);
        _controls.pack_start(*frame, Gtk::PACK_SHRINK);
    }

    void wireSignals()
    {
        _btn_open.signal_clicked().connect(sigc::mem_fun(*this, &TraceWindow::onOpen));
        _btn_preview.signal_clicked().connect(sigc::mem_fun(*this, &TraceWindow::onPreview));
        _btn_trace.signal_clicked().connect(sigc::mem_fun(*this, &TraceWindow::onTrace));
        _btn_cancel.signal_clicked().connect(sigc::mem_fun(*this, &TraceWindow::onCancel));
        _btn_export.signal_clicked().connect(sigc::mem_fun(*this, &TraceWindow::onExport));

        _mode_mono.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::updateSensitivity));
        _mode_color.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::updateSensitivity));
        _threshold.signal_value_changed().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _scans.signal_value_changed().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _invert.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _stack.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _smooth.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _policy.signal_changed().connect(sigc::mem_fun(*this, &TraceWindow::updateSensitivity));

        // Preview live-updates on path option changes too (cheap: no Potrace).
        _optimize.signal_toggled().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _opt_tolerance.signal_value_changed().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _smooth_corners.signal_value_changed().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
        _speckles.signal_value_changed().connect(sigc::mem_fun(*this, &TraceWindow::onParamChanged));
    }

    // ------------------------------------------------------------------
    ivf::Settings currentSettings() const
    {
        ivf::Settings s;
        s.mode = _mode_color.get_active() ? ivf::Mode::Color : ivf::Mode::Mono;
        s.threshold = _threshold.get_value();
        s.scans = int(_scans.get_value());
        s.stack = _stack.get_active();
        s.smooth = _smooth.get_active();
        s.invert = _invert.get_active();
        s.optimize = _optimize.get_active();
        s.opt_tolerance = _opt_tolerance.get_value();
        s.smooth_corners = _smooth_corners.get_value();
        s.speckles_size = int(_speckles.get_value());

        switch (_policy.get_active_row_number()) {
            case 1:  s.background_policy = ivf::BackgroundPolicy::KeepAll; break;
            case 2:  s.background_policy = ivf::BackgroundPolicy::RemoveDominantBorderColor; break;
            case 3:  s.background_policy = ivf::BackgroundPolicy::RemoveDetectedByColor; break;
            default: s.background_policy = ivf::BackgroundPolicy::RemoveTransparent; break; // "auto"
        }

        if (s.background_policy == ivf::BackgroundPolicy::RemoveDetectedByColor) {
            std::string const text = _bg_color.get_text();
            uint32_t v = 0;
            if (ivf::parse_hex_color_public(text, v)) {
                s.remove_bg_color = v;
            }
            s.background_area_threshold = _area_threshold.get_value();
        }
        return s;
    }

    void updateSensitivity()
    {
        bool const mono = _mode_mono.get_active();
        _threshold_frame->set_sensitive(mono);
        _invert.set_sensitive(true); // useful in both modes
        _scans_frame->set_sensitive(!mono);
        _stack.set_sensitive(!mono);
        _smooth.set_sensitive(true); // usable in both modes (opt-in in color)
        _policy_frame->set_sensitive(true);
        _bgcolor_frame->set_sensitive(_policy.get_active_row_number() == 3);
        _areathr_frame->set_sensitive(_policy.get_active_row_number() == 3);

        _btn_preview.set_sensitive(bool(_pixbuf));
        _btn_trace.set_sensitive(bool(_pixbuf) && !_worker);
    }

    void onParamChanged()
    {
        // Live preview update (never runs Potrace: it is fast).
        if (_pixbuf && !_worker) {
            onPreview();
        }
    }

    // ------------------------------------------------------------------
    void onOpen()
    {
        Gtk::FileChooserDialog dlg(*this, "Open image", Gtk::FILE_CHOOSER_ACTION_OPEN);
        dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        dlg.add_button("_Open", Gtk::RESPONSE_OK);
        auto filter = Gtk::FileFilter::create();
        filter->set_name("Images");
        filter->add_pixbuf_formats();
        dlg.add_filter(filter);

        if (dlg.run() != Gtk::RESPONSE_OK) {
            return;
        }

        try {
            auto pb = ivf::load_image(dlg.get_filename());
            if (!pb) {
                _status.set_text("Could not load image: " + dlg.get_filename());
                return;
            }
            _pixbuf = pb;
            _file_label.set_text(dlg.get_filename());
            _status.set_text(Glib::ustring::compose(
                "%1 × %2%3",
                _pixbuf->get_width(), _pixbuf->get_height(),
                ivf::has_alpha(_pixbuf) ? ", alpha" : ""));
            _preview_pixbuf.clear();
            _result.reset();
            _btn_export.set_sensitive(false);
            setPreview(_pixbuf); // show original until a preview is computed
            updateSensitivity();
            onPreview();
        } catch (Glib::Error const &e) {
            _status.set_text(e.what());
        }
    }

    void setPreview(Glib::RefPtr<Gdk::Pixbuf> const &pb)
    {
        // Scale to fit the preview pane without upsampling beyond 2x.
        int const max_w = 500, max_h = 560;
        double const sx = double(max_w) / std::max(1, pb->get_width());
        double const sy = double(max_h) / std::max(1, pb->get_height());
        double const s = std::min(2.0, std::min(sx, sy));
        auto shown = pb;
        if (s < 1.0) {
            shown = pb->scale_simple(int(pb->get_width() * s), int(pb->get_height() * s),
                                     Gdk::INTERP_BILINEAR);
        }
        // IVF PATCH (preview-checkerboard): images with alpha are composited
        // over the classic transparency checkerboard instead of whatever the
        // theme paints behind the widget — a transparent background can no
        // longer be mistaken for white/black content, and semi-transparent
        // edges are honest. Opaque images are untouched (no cost).
        if (shown->get_has_alpha()) {
            shown = compositeOverCheckerboard(shown);
        }
        _image.set(shown);
    }

    static Glib::RefPtr<Gdk::Pixbuf>
    compositeOverCheckerboard(Glib::RefPtr<Gdk::Pixbuf> const &pb)
    {
        using Glib::RefPtr;
        int const w = pb->get_width();
        int const h = pb->get_height();
        auto out = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, w, h);
        int const ors = pb->get_rowstride(), onch = pb->get_n_channels();
        int const ors_out = out->get_rowstride();
        auto const *src = pb->get_pixels();
        auto *dst = out->get_pixels();
        // GIMP-style 8 px checkerboard in light gray tones.
        auto shade = [](int x, int y) {
            return ((x / 8) + (y / 8)) % 2 == 0 ? 204 : 255;
        };
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                auto const *p = src + y * ors + x * onch;
                auto *q = dst + y * ors_out + x * 3;
                int const bg = shade(x, y);
                int const a = onch == 4 ? p[3] : 255;
                for (int c = 0; c < 3; c++) {
                    q[c] = (p[c] * a + bg * (255 - a)) / 255;
                }
            }
        }
        return out;
    }

    void onPreview()
    {
        if (!_pixbuf) return;
        try {
            ivf::Tracer tracer(currentSettings());
            auto pv = tracer.preview(_pixbuf);
            if (pv) {
                _preview_pixbuf = pv;
                setPreview(pv);
            }
        } catch (Glib::Error const &e) {
            _status.set_text(Glib::ustring::compose("preview error: %1", e.what()));
        }
    }

    // ------------------------------------------------------------------
    void onTrace()
    {
        if (!_pixbuf || _worker) return;

        _btn_trace.set_sensitive(false);
        _btn_open.set_sensitive(false);
        _btn_preview.set_sensitive(false);
        _btn_cancel.set_sensitive(true);
        _btn_export.set_sensitive(false);
        _progress.set_fraction(0.0);
        _status.set_text("tracing…");

        auto worker = std::make_unique<TraceWorker>(
            currentSettings(), _pixbuf, _done, _failed, _pct);
        _worker = std::move(worker);
        _thread = Glib::Thread::create(
            sigc::mem_fun(*_worker, &TraceWorker::run), true /* joinable */);
    }

    void onCancel()
    {
        if (_worker) {
            _worker->cancel();
            _status.set_text("cancelling…");
        }
    }

    bool onPollProgress()
    {
        if (_worker) {
            int pct = _pct.load(std::memory_order_relaxed);
            _progress.set_fraction(std::min(1.0, pct / 100.0));
        }
        return true; // keep polling
    }

    void onTraceDone()
    {
        joinWorker();
        if (!_worker_result) return;

        _result = _worker_result;
        _worker_result.reset();
        _progress.set_fraction(1.0);
        _status.set_text(Glib::ustring::compose(
            "done: %1 path(s), %2 node(s)", _result->size(), _result->nodeCount()));
        _btn_export.set_sensitive(bool(_pixbuf) && _result && _result->size() > 0);
        _btn_trace.set_sensitive(true);
        _btn_open.set_sensitive(true);
        _btn_preview.set_sensitive(true);
        _btn_cancel.set_sensitive(false);
    }

    void onTraceFailed()
    {
        joinWorker();
        if (!_worker_error.empty()) {
            _status.set_text(Glib::ustring::compose("error: %1", _worker_error));
            _worker_error.clear();
        } else {
            _status.set_text("cancelled");
        }
        _progress.set_fraction(0.0);
        _btn_trace.set_sensitive(true);
        _btn_open.set_sensitive(true);
        _btn_preview.set_sensitive(true);
        _btn_cancel.set_sensitive(false);
    }

    // Runs on the UI thread (from the dispatchers): collect worker output.
    void joinWorker()
    {
        if (_thread) {
            _thread->join();
            _thread = nullptr;
        }
        if (_worker) {
            _worker_result = _worker->takeResult();
            _worker_error = _worker->error();
            _worker.reset();
        }
    }

    // ------------------------------------------------------------------
    void onExport()
    {
        if (!_pixbuf || !_result || _result->size() == 0) return;

        Gtk::FileChooserDialog dlg(*this, "Export SVG", Gtk::FILE_CHOOSER_ACTION_SAVE);
        dlg.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        dlg.add_button("_Save", Gtk::RESPONSE_OK);
        dlg.set_do_overwrite_confirmation(true);
        dlg.set_current_name("traced.svg");

        if (dlg.run() != Gtk::RESPONSE_OK) {
            return;
        }

        try {
            auto svg = _result->getSVG(_pixbuf->get_width(), _pixbuf->get_height());
            std::ofstream out(dlg.get_filename(), std::ios::binary);
            if (!out) {
                _status.set_text("cannot write file: " + dlg.get_filename());
                return;
            }
            out << svg;
            out.close();
            _status.set_text("saved: " + dlg.get_filename());
        } catch (std::exception const &e) {
            _status.set_text(std::string("error: ") + e.what());
        }
    }

    // ------------------------------------------------------------------
    // Widgets
    Gtk::Paned _paned{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Frame _frame;
    Gtk::Image _image;
    Gtk::Box _controls{Gtk::ORIENTATION_VERTICAL, 6};

    Gtk::Button _btn_open, _btn_preview, _btn_trace, _btn_cancel, _btn_export;
    Gtk::Label _file_label, _status;
    Gtk::ProgressBar _progress;

    Gtk::RadioButton _mode_mono, _mode_color;
    Gtk::RadioButtonGroup _mode_color_group;
    Gtk::SpinButton _threshold, _scans, _opt_tolerance, _smooth_corners, _speckles, _area_threshold;
    Gtk::CheckButton _invert, _stack, _smooth, _optimize;
    Gtk::ComboBoxText _policy;
    Gtk::Entry _bg_color;

    Gtk::Frame *_mode_frame = nullptr, *_threshold_frame = nullptr, *_scans_frame = nullptr,
               *_opttol_frame = nullptr, *_alphamax_frame = nullptr, *_speckles_frame = nullptr,
               *_policy_frame = nullptr, *_bgcolor_frame = nullptr, *_areathr_frame = nullptr;

    // State
    Glib::RefPtr<Gdk::Pixbuf> _pixbuf;        // original image
    Glib::RefPtr<Gdk::Pixbuf> _preview_pixbuf; // filter preview
    std::shared_ptr<Inkscape::Trace::SvgBuilder> _result;

    // Worker thread plumbing
    Glib::Thread *_thread = nullptr;
    std::unique_ptr<TraceWorker> _worker;
    std::shared_ptr<Inkscape::Trace::SvgBuilder> _worker_result;
    std::string _worker_error;
    Glib::Dispatcher _done, _failed;
    std::atomic<int> _pct{0};
    sigc::connection _poll;
};

} // namespace

int main(int argc, char **argv)
{
    auto app = Gtk::Application::create(argc, argv, "org.inkscapevectorizerfull.gui");

    TraceWindow window;
    return app->run(window);
}
