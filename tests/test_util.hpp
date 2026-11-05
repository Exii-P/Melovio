#pragma once
#include "core/export.hpp"
#include "core/history.hpp"
#include "core/transport.hpp"
#include "core/project_io.hpp"
#include "core/voicebank_library.hpp"
#include <cmath>
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>
#include <functional>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <thread>

namespace tu {
    using namespace melovio;

    struct TempDir {
        fs::path path;
        TempDir() { path = fs::temp_directory_path() / make_id("melovio_test"); fs::create_directories(path); }
        ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
        fs::path operator/(const std::string& s) const { return path / s; }
    };

    inline VoicebankLibrary& bundled_library() {
        static VoicebankLibrary lib = [] { VoicebankLibrary l; l.add_search_dir(fs::path(MELOVIO_BUNDLED_VOICEBANKS), true); l.refresh(); return l; }();
        return lib;
    }
    inline std::map<std::string, Voicebank> banks(std::initializer_list<const char*> ids = { "yune" }) {
        std::map<std::string, Voicebank> m;
        for (auto* id : ids) { const Voicebank* v = bundled_library().find(id); REQUIRE(v != nullptr); m[id] = *v; }
        return m;
    }

    // Three-note project: A4 B4 C5, one quarter each at 120 bpm (0.5 s per note).
    inline Project small_project(const std::string& vb = "yune") {
        Project p; p.meta.title = "test";
        Track& t = p.add_track("Lead", vb);
        const char* ly[] = { "ら", "ら", "ら" }; const int pitch[] = { 69, 71, 72 };
        for (int i = 0; i < 3; ++i) { Note n; n.tick = i * 480; n.length = 460; n.pitch = pitch[i]; n.lyric = ly[i]; p.add_note(t, n); }
        p.ensure_voicebank_ref(vb, "1.0.0", vb);
        return p;
    }

    // Fundamental frequency by autocorrelation over [a,b) seconds of a mono signal.
    inline double f0_of(const std::vector<float>& x, int sr, double a, double b, double lo = 120, double hi = 900) {
        const size_t i0 = static_cast<size_t>(a * sr), i1 = std::min(x.size(), static_cast<size_t>(b * sr));
        double best = 0; int best_lag = 0;
        for (int lag = static_cast<int>(sr / hi); lag < static_cast<int>(sr / lo); ++lag) {
            double c = 0; for (size_t i = i0; i + static_cast<size_t>(lag) < i1; ++i) c += static_cast<double>(x[i]) * x[i + static_cast<size_t>(lag)];
            if (c > best) { best = c; best_lag = lag; }
        }
        return best_lag ? static_cast<double>(sr) / best_lag : 0.0;
    }
    inline double rms_of(const std::vector<float>& x, int sr, double a, double b) {
        const size_t i0 = static_cast<size_t>(a * sr), i1 = std::min(x.size(), static_cast<size_t>(b * sr));
        double s = 0; for (size_t i = i0; i < i1; ++i) s += static_cast<double>(x[i]) * x[i];
        return i1 > i0 ? std::sqrt(s / static_cast<double>(i1 - i0)) : 0.0;
    }
}
