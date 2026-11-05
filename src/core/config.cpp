#include "config.hpp"
#include "voicebank_library.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace melovio {
    using json = nlohmann::json;

    fs::path config_file_path() { return default_config_dir() / "settings.json"; }

    void AppConfig::add_recent(const fs::path& p, size_t max_entries) {
        std::string s = path_to_utf8(fs::absolute(p));
        recent_projects.erase(std::remove(recent_projects.begin(), recent_projects.end(), s), recent_projects.end());
        recent_projects.insert(recent_projects.begin(), s);
        if (recent_projects.size() > max_entries) recent_projects.resize(max_entries);
    }
    void AppConfig::prune_missing_recent() {
        std::error_code ec;
        recent_projects.erase(std::remove_if(recent_projects.begin(), recent_projects.end(), [&](const std::string& s) { return !fs::exists(path_from_utf8(s), ec); }), recent_projects.end());
    }
    std::vector<std::string> AppConfig::recent_existing() const { AppConfig c = *this; c.prune_missing_recent(); return c.recent_projects; }

    Result<AppConfig> load_config(const fs::path& file) {
        AppConfig c; std::error_code ec;
        if (!fs::exists(file, ec)) return Result<AppConfig>::success(c);
        std::string err; std::string t = read_text_file(file, &err);
        json j = json::parse(t, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return Result<AppConfig>::fail("settings file is corrupt: " + path_to_utf8(file));
        auto strs = [&](const char* k) { std::vector<std::string> o; if (j.contains(k) && j[k].is_array()) for (auto& e : j[k]) if (e.is_string()) o.push_back(e.get<std::string>()); return o; };
        auto str = [&](const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
        c.recent_projects = strs("recent_projects"); c.voicebank_dirs = strs("voicebank_dirs");
        c.last_project_dir = str("last_project_dir"); c.last_export_dir = str("last_export_dir"); c.last_import_dir = str("last_import_dir"); c.layout_ini = str("layout_ini");
        if (j.contains("autosave_seconds") && j["autosave_seconds"].is_number_integer()) c.autosave_seconds = std::clamp(j["autosave_seconds"].get<int>(), 0, 3600);
        if (j.contains("audio_sample_rate") && j["audio_sample_rate"].is_number_integer()) c.audio_sample_rate = j["audio_sample_rate"].get<int>();
        if (j.contains("ui_scale") && j["ui_scale"].is_number()) c.ui_scale = std::clamp(j["ui_scale"].get<float>(), 0.5f, 3.f);
        if (j.contains("export") && j["export"].is_object()) {
            const json& e = j["export"];
            c.export_defaults.sample_rate = e.value("sample_rate", c.export_defaults.sample_rate); c.export_defaults.bit_depth = e.value("bit_depth", c.export_defaults.bit_depth);
            c.export_defaults.channels = e.value("channels", c.export_defaults.channels); c.export_defaults.normalize = e.value("normalize", c.export_defaults.normalize);
            c.export_defaults.normalize_peak_db = e.value("normalize_peak_db", c.export_defaults.normalize_peak_db); c.export_defaults.dither = e.value("dither", c.export_defaults.dither);
            c.export_defaults.tail_sec = e.value("tail_sec", c.export_defaults.tail_sec);
        }
        return Result<AppConfig>::success(std::move(c));
    }

    AppConfig load_config_or_default(const fs::path& file, std::string* warning) {
        auto r = load_config(file);
        if (r) return *r;
        if (warning) *warning = r.error;
        return AppConfig();
    }

    Status save_config(const AppConfig& c, const fs::path& file) {
        json j;
        j["recent_projects"] = c.recent_projects; j["voicebank_dirs"] = c.voicebank_dirs;
        j["last_project_dir"] = c.last_project_dir; j["last_export_dir"] = c.last_export_dir; j["last_import_dir"] = c.last_import_dir;
        j["autosave_seconds"] = c.autosave_seconds; j["audio_sample_rate"] = c.audio_sample_rate; j["ui_scale"] = c.ui_scale; j["layout_ini"] = c.layout_ini;
        const auto& e = c.export_defaults;
        j["export"] = { { "sample_rate", e.sample_rate }, { "bit_depth", e.bit_depth }, { "channels", e.channels }, { "normalize", e.normalize }, { "normalize_peak_db", e.normalize_peak_db }, { "dither", e.dither }, { "tail_sec", e.tail_sec } };
        return write_text_file_atomic(file, j.dump(2) + "\n");
    }
}