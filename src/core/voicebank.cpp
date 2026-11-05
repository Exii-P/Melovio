#include "voicebank.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace melovio {
    using json = nlohmann::json;

    static std::vector<std::string> str_array(const json& j, const char* k) {
        std::vector<std::string> o; auto it = j.find(k);
        if (it == j.end() || !it->is_array()) return o;
        for (auto& e : *it) if (e.is_string()) o.push_back(e.get<std::string>());
        return o;
    }
    static std::string str_or(const json& j, const char* k, const std::string& d = "") {
        auto it = j.find(k); return (it != j.end() && it->is_string()) ? it->get<std::string>() : d;
    }

    Result<VoicebankManifest> manifest_from_json(const std::string& text) {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) return Result<VoicebankManifest>::fail("voicebank.json is not valid JSON");
        if (!j.is_object()) return Result<VoicebankManifest>::fail("voicebank.json: top level must be an object");
        if (str_or(j, "format") != kVoicebankFormatId) return Result<VoicebankManifest>::fail("not a Melovio voicebank (format id is not 'melovio-voicebank')");
        if (!j.contains("format_version") || !j["format_version"].is_number_integer()) return Result<VoicebankManifest>::fail("voicebank.json: missing format_version");
        const int fv = j["format_version"].get<int>();
        if (fv > kVoicebankFormatVersion) return Result<VoicebankManifest>::fail("voicebank format version " + std::to_string(fv) + " is newer than this Melovio supports (" + std::to_string(kVoicebankFormatVersion) + ")");
        VoicebankManifest m;
        m.id = str_or(j, "id"); m.version = str_or(j, "version", "1.0.0"); m.name = str_or(j, "name"); m.native_name = str_or(j, "native_name");
        m.author = str_or(j, "author"); m.license = str_or(j, "license"); m.description = str_or(j, "description");
        m.tagline = str_or(j, "tagline"); m.homepage = str_or(j, "homepage"); m.tags = str_array(j, "tags");
        if (j.contains("character") && j["character"].is_object()) m.character_notes = str_or(j["character"], "notes");
        auto langs = str_array(j, "languages"); if (!langs.empty()) m.languages = langs;
        if (j.contains("synthesis") && j["synthesis"].is_object()) {
            const json& s = j["synthesis"];
            m.engine = str_or(s, "engine", m.engine); m.engine_version = str_or(s, "engine_version", m.engine_version);
            m.kind = str_or(s, "kind", m.kind); m.profile = str_or(s, "profile", m.profile);
        }
        if (j.contains("phonetics") && j["phonetics"].is_object()) {
            const json& p = j["phonetics"];
            if (p.contains("recordings") && p["recordings"].is_array())
                for (auto& r : p["recordings"]) if (r.is_object()) m.recordings.push_back({ str_or(r, "phoneme"), str_or(r, "file") });
            if (p.contains("mappings") && p["mappings"].is_object())
                for (auto it = p["mappings"].begin(); it != p["mappings"].end(); ++it) if (it->is_string()) m.phonetic_mappings[it.key()] = it->get<std::string>();
            m.preview_lyric = str_or(p, "preview_lyric", m.preview_lyric);
        }
        if (j.contains("compatibility") && j["compatibility"].is_object()) m.min_melovio = str_or(j["compatibility"], "min_melovio", m.min_melovio);
        m.completeness = str_or(j, "completeness");
        return Result<VoicebankManifest>::success(std::move(m));
    }

    std::string manifest_to_json(const VoicebankManifest& m) {
        json j;
        j["format"] = kVoicebankFormatId; j["format_version"] = kVoicebankFormatVersion;
        j["id"] = m.id; j["version"] = m.version; j["name"] = m.name; j["native_name"] = m.native_name;
        j["author"] = m.author; j["license"] = m.license; j["description"] = m.description; j["tagline"] = m.tagline;
        if (!m.homepage.empty()) j["homepage"] = m.homepage;
        j["languages"] = m.languages; j["tags"] = m.tags;
        j["character"] = { { "notes", m.character_notes } };
        j["synthesis"] = { { "engine", m.engine }, { "engine_version", m.engine_version }, { "kind", m.kind }, { "profile", m.profile } };
        json rec = json::array(); for (auto& r : m.recordings) rec.push_back({ { "phoneme", r.phoneme }, { "file", r.file } });
        j["phonetics"] = { { "recordings", rec }, { "mappings", m.phonetic_mappings }, { "preview_lyric", m.preview_lyric } };
        j["compatibility"] = { { "min_melovio", m.min_melovio } };
        j["completeness"] = m.completeness;
        return j.dump(2) + "\n";
    }

    Result<Voicebank> load_voicebank(const fs::path& dir, bool bundled) {
        std::error_code ec;
        const fs::path mf = dir / kVoicebankManifestName;
        if (!fs::exists(mf, ec)) return Result<Voicebank>::fail("no " + std::string(kVoicebankManifestName) + " in " + path_to_utf8(dir));
        std::string err; const std::string text = read_text_file(mf, &err);
        if (!err.empty()) return Result<Voicebank>::fail(err);
        auto m = manifest_from_json(text);
        if (!m) return Result<Voicebank>::fail(path_to_utf8(dir) + ": " + m.error);
        Voicebank vb; vb.manifest = *m; vb.root = dir; vb.bundled = bundled;
        auto pr = read_profile(vb.profile_path());
        if (pr) { vb.profile = *pr; vb.profile_ok = true; } else vb.profile_error = pr.error;
        vb.diagnostics = validate_voicebank(vb);
        return Result<Voicebank>::success(std::move(vb));
    }

    std::vector<Diagnostic> validate_voicebank(const Voicebank& vb) {
        std::vector<Diagnostic> d; const auto& m = vb.manifest;
        auto add = [&](Severity s, const char* c, std::string msg) { d.push_back({ s, c, std::move(msg) }); };
        if (m.id.empty()) add(Severity::Error, "vb.id_missing", "manifest has no id");
        else for (char c : m.id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) { add(Severity::Error, "vb.id_syntax", "id '" + m.id + "' may only contain a-z 0-9 - _"); break; }
        if (!valid_version(m.version)) add(Severity::Error, "vb.version", "version '" + m.version + "' is not of the form 1.2 / 1.2.3");
        if (m.name.empty()) add(Severity::Error, "vb.name_missing", "manifest has no name");
        if (m.license.empty()) add(Severity::Warning, "vb.license_missing", "no license stated; redistribution terms are unclear");
        if (m.author.empty()) add(Severity::Warning, "vb.author_missing", "no author stated");
        if (std::find(m.languages.begin(), m.languages.end(), "ja") == m.languages.end())
            add(Severity::Error, "vb.language", "none of the declared languages is supported (the engine currently sings Japanese only)");
        if (m.engine != "wauvio-vocal") add(Severity::Error, "vb.engine", "engine '" + m.engine + "' is not available; only 'wauvio-vocal' is supported");
        else if (m.engine_version != "v2") add(Severity::Error, "vb.engine_version", "engine version '" + m.engine_version + "' is not supported (need v2)");
        if (m.kind != "parametric-formant")
            add(Severity::Error, "vb.kind", "synthesis kind '" + m.kind + "' cannot be rendered: Wauvio-Vocal v2 is a parametric formant synthesiser and has no sample-based voice path");
        if (!valid_version(m.min_melovio)) add(Severity::Warning, "vb.min_melovio", "compatibility.min_melovio is not a valid version");
        else if (compare_versions(m.min_melovio, MELOVIO_VERSION) > 0) add(Severity::Error, "vb.too_new", "needs Melovio " + m.min_melovio + " or newer (this is " MELOVIO_VERSION ")");
        // profile
        if (m.profile.empty() || is_absolute_utf8(m.profile) || m.profile.find("..") != std::string::npos) add(Severity::Error, "vb.profile_path", "synthesis.profile must be a relative path inside the package");
        else if (!vb.profile_ok) add(Severity::Error, "vb.profile_invalid", "voice profile '" + m.profile + "' cannot be loaded: " + vb.profile_error);
        else {
            if (vb.profile.id != m.id) add(Severity::Warning, "vb.profile_id", "profile id '" + vb.profile.id + "' differs from manifest id '" + m.id + "'");
            if (vb.profile.language != "ja") add(Severity::Warning, "vb.profile_language", "profile language is '" + vb.profile.language + "', the engine's phoneme rules are Japanese");
        }
        // samples
        std::error_code ec;
        for (auto& r : m.recordings) {
            if (r.file.empty() || is_absolute_utf8(r.file) || r.file.find("..") != std::string::npos) { add(Severity::Error, "vb.recording_path", "recording for '" + r.phoneme + "' has an unsafe path"); continue; }
            if (!fs::exists(vb.root / path_from_utf8(r.file), ec)) add(Severity::Error, "vb.recording_missing", "recording file missing: " + r.file);
        }
        if (!m.recordings.empty() && m.kind == "parametric-formant")
            add(Severity::Warning, "vb.recordings_unused", "recordings are listed but the parametric engine does not use them");
        for (auto& kv : m.phonetic_mappings) if (kv.first.empty() || kv.second.empty()) add(Severity::Error, "vb.mapping", "phonetic mapping with an empty side");
        return d;
    }
}
