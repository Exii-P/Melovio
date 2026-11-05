#include "export.hpp"
#include <algorithm>
#include <cmath>

namespace melovio {
    const int kExportSampleRates[6] = { 22050, 32000, 44100, 48000, 88200, 96000 };

    std::vector<Diagnostic> validate_export_settings(const ExportSettings& s, const Project& p) {
        std::vector<Diagnostic> d;
        auto err = [&](const char* c, std::string m) { d.push_back({ Severity::Error, c, std::move(m) }); };
        if (std::find(std::begin(kExportSampleRates), std::end(kExportSampleRates), s.sample_rate) == std::end(kExportSampleRates)) err("export.sample_rate", "unsupported sample rate " + std::to_string(s.sample_rate));
        if (s.bit_depth != 16 && s.bit_depth != 24 && s.bit_depth != 32) err("export.bit_depth", "bit depth must be 16, 24 or 32 (float)");
        if (s.channels != 1 && s.channels != 2) err("export.channels", "channels must be 1 or 2");
        if (s.tail_sec < 0 || s.tail_sec > 30) err("export.tail", "tail must be between 0 and 30 seconds");
        if (s.normalize && (s.normalize_peak_db > 0 || s.normalize_peak_db < -60)) err("export.normalize", "normalise target must be between -60 and 0 dBFS");
        if (s.scope == ExportScope::Region && s.region_end <= s.region_start) err("export.region", "region is empty");
        if (s.scope == ExportScope::Region && s.region_start < 0) err("export.region", "region starts before the project");
        if (s.scope == ExportScope::Track && !p.find_track(s.track_id)) err("export.track", "no such track");
        if (s.scope == ExportScope::Project && p.end_tick() == 0) err("export.empty", "the project has no notes");
        return d;
    }

    Result<ExportResult> export_wav(const Project& p, const Renderer& renderer, const ExportSettings& s, const fs::path& out, const ProgressFn& progress, const CancelToken* cancel) {
        using R = Result<ExportResult>;
        auto v = validate_export_settings(s, p);
        if (has_errors(v)) { std::string m; for (auto& x : v) if (x.severity == Severity::Error) m += (m.empty() ? "" : "; ") + x.message; return R::fail(m); }

        RenderOptions ro; ro.sample_rate = s.sample_rate; ro.tail_sec = s.tail_sec;
        ExportResult res; res.file = out;
        StereoAudio audio;
        if (s.scope == ExportScope::Region) {
            ro.t0 = p.tempo_map.tick_to_sec(s.region_start); ro.t1 = p.tempo_map.tick_to_sec(s.region_end);
        }
        if (s.scope == ExportScope::Track) {
            // Solo-render one track: build a project view containing only it, with mute/solo ignored.
            Project one = p; one.tracks.clear();
            Track t = *p.find_track(s.track_id); t.mute = false; t.solo = false; one.tracks.push_back(t);
            auto r = renderer.render_mix(one, ro, progress, cancel, &res.diagnostics);
            if (!r) return R::fail(r.error);
            audio = std::move(*r);
        } else {
            auto r = renderer.render_mix(p, ro, progress, cancel, &res.diagnostics);
            if (!r) return R::fail(r.error);
            audio = std::move(*r);
        }
        if (cancel && cancel->cancelled()) return R::fail("cancelled");

        std::vector<float> data = audio.interleaved(s.channels);
        double peak = 0; for (float x : data) peak = std::max(peak, static_cast<double>(std::fabs(x)));
        res.peak = peak;
        if (s.normalize && peak > 1e-9) {
            const float g = static_cast<float>(std::pow(10.0, s.normalize_peak_db / 20.0) / peak);
            for (float& x : data) x *= g;
            res.peak = std::pow(10.0, s.normalize_peak_db / 20.0);
        } else if (peak > 1.0 && s.bit_depth != 32) {
            res.clipped = true;
            res.diagnostics.push_back({ Severity::Warning, "export.clipping", "the mix peaks above 0 dBFS (" + std::to_string(20.0 * std::log10(peak)).substr(0, 5) + " dB) and will clip; enable normalise or lower track gain" });
        }
        WavFormat f; f.sample_rate = s.sample_rate; f.channels = s.channels; f.bit_depth = s.bit_depth; f.dither = s.dither;
        Status st = write_wav_file(out, data, f);
        if (!st) return R::fail(st.message);
        res.frames = static_cast<size_t>(data.size() / static_cast<size_t>(s.channels));
        res.duration_sec = static_cast<double>(res.frames) / s.sample_rate;
        return R::success(std::move(res));
    }
}
