// Application settings (recent projects, folders, defaults). Stored as JSON in the per-user config directory.
#pragma once
#include "export.hpp"
#include "util.hpp"

namespace melovio {
    struct AppConfig {
        std::vector<std::string> recent_projects;       // absolute paths (machine-local settings only; never stored in projects)
        std::vector<std::string> voicebank_dirs;        // extra search folders
        std::string last_project_dir, last_export_dir, last_import_dir;
        int autosave_seconds = 60;                      // 0 = off
        ExportSettings export_defaults;
        int audio_sample_rate = 44100;
        float ui_scale = 1.0f;
        std::string layout_ini;                         // ImGui docking layout

        void add_recent(const fs::path& p, size_t max_entries = 12);
        void prune_missing_recent();
        std::vector<std::string> recent_existing() const;
    };

    fs::path config_file_path();
    Result<AppConfig> load_config(const fs::path& file);        // missing file -> defaults; corrupt file -> defaults + error message
    AppConfig load_config_or_default(const fs::path& file, std::string* warning = nullptr);
    Status save_config(const AppConfig& c, const fs::path& file);
}
