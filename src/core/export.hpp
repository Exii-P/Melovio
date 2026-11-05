// WAV export of a project, a region, or a single vocal track.
#pragma once
#include "renderer.hpp"
#include "wav_writer.hpp"

namespace melovio {
    enum class ExportScope { Project, Region, Track };

    struct ExportSettings {
        int sample_rate = 44100;             // 22050, 32000, 44100, 48000, 88200, 96000
        int bit_depth = 16;                  // 16, 24, 32 (float)
        int channels = 2;                    // 1 = mono sum, 2 = stereo
        ExportScope scope = ExportScope::Project;
        Tick region_start = 0, region_end = 0;   // for Region (project ticks)
        std::string track_id;                    // for Track: that track alone, ignoring mute/solo, with its gain/pan
        bool normalize = false;              // scale so the peak is at normalize_peak_db
        double normalize_peak_db = -1.0;
        double tail_sec = 1.0;               // silence/release after the last note (Project and Track scopes)
        bool dither = true;                  // TPDF, 16/24-bit only
    };

    extern const int kExportSampleRates[6];
    std::vector<Diagnostic> validate_export_settings(const ExportSettings& s, const Project& p);

    struct ExportResult {
        fs::path file;
        double duration_sec = 0, peak = 0;
        size_t frames = 0;
        bool clipped = false;                // peak exceeded full scale before clamping (and normalize was off)
        std::vector<Diagnostic> diagnostics;
    };

    // Renders and writes `out_file` (atomically: a cancelled/failed export leaves no partial file).
    Result<ExportResult> export_wav(const Project& p, const Renderer& renderer, const ExportSettings& s, const fs::path& out_file,
                                    const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);
}
