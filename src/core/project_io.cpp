#include "project_io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace melovio {
    using json = nlohmann::json;

    namespace {
        struct ParseError { std::string msg; };

        const json& need(const json& j, const char* key, const std::string& ctx) {
            auto it = j.find(key);
            if (it == j.end()) throw ParseError{ ctx + ": missing '" + key + "'" };
            return *it;
        }
        template <class T> T get_or(const json& j, const char* key, T def, const std::string& ctx) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return def;
            try { return it->get<T>(); } catch (const json::exception&) { throw ParseError{ ctx + "." + key + ": wrong type" }; }
        }
        std::map<std::string, float> params_from(const json& j, const std::string& ctx) {
            std::map<std::string, float> m;
            if (j.is_null()) return m;
            if (!j.is_object()) throw ParseError{ ctx + ": expected object" };
            for (auto it = j.begin(); it != j.end(); ++it) { if (!it->is_number()) throw ParseError{ ctx + "." + it.key() + ": expected number" }; m[it.key()] = it->get<float>(); }
            return m;
        }
    }

    std::string project_to_json(const Project& p, const fs::path& base_dir, std::vector<Diagnostic>* warnings) {
        json j;
        j["format"] = kProjectFormatId;
        j["format_version"] = kProjectFormatVersion;
        j["app_version"] = MELOVIO_VERSION;
        j["meta"] = { { "title", p.meta.title }, { "composer", p.meta.composer }, { "lyricist", p.meta.lyricist }, { "comment", p.meta.comment },
                      { "created", p.meta.created }, { "modified", p.meta.modified } };
        j["settings"] = { { "ppq", p.tempo_map.ppq() }, { "sample_rate", p.sample_rate } };
        j["tempo_map"] = json::array();
        for (auto& t : p.tempo_map.tempi()) j["tempo_map"].push_back({ { "tick", t.tick }, { "bpm", t.bpm } });
        j["time_signatures"] = json::array();
        for (auto& s : p.tempo_map.signatures()) j["time_signatures"].push_back({ { "bar", s.bar }, { "numerator", s.numerator }, { "denominator", s.denominator } });
        j["loop"] = { { "enabled", p.loop.enabled }, { "start", p.loop.start }, { "end", p.loop.end } };
        j["voicebanks"] = json::array();
        for (auto& v : p.voicebanks) {
            std::string hint = v.path_hint;
            if (!hint.empty() && is_absolute_utf8(hint)) {
                std::optional<std::string> rel; if (!base_dir.empty()) rel = relative_utf8(path_from_utf8(hint), base_dir);
                if (rel) hint = *rel; else {
                    hint.clear();
                    if (warnings) warnings->push_back({ Severity::Warning, "project.path_dropped", "voicebank '" + v.id + "': absolute location cannot be stored relative to the project and was not saved" });
                }
            }
            json o = { { "id", v.id }, { "version", v.version }, { "name", v.name } };
            if (!hint.empty()) o["path"] = hint;
            j["voicebanks"].push_back(o);
        }
        j["next_note_id"] = p.next_note_id;
        j["tracks"] = json::array();
        for (auto& t : p.tracks) {
            json tj = { { "id", t.id }, { "name", t.name }, { "voicebank", t.voicebank_id }, { "persona", t.persona }, { "persona_amount", t.persona_amount },
                        { "style", t.style }, { "natural", t.natural }, { "natural_take", t.natural_take }, { "auto_expression", t.auto_expression },
                        { "gain_db", t.gain_db }, { "pan", t.pan }, { "mute", t.mute }, { "solo", t.solo } };
            tj["notes"] = json::array();
            for (auto& n : t.notes) {
                json nj = { { "id", n.id }, { "tick", n.tick }, { "length", n.length }, { "pitch", n.pitch }, { "lyric", n.lyric }, { "velocity", n.velocity } };
                if (!n.phonemes.empty()) nj["phonemes"] = n.phonemes;
                if (n.offset_ms != 0.0) nj["offset_ms"] = n.offset_ms;
                if (!n.params.empty()) nj["params"] = n.params;
                tj["notes"].push_back(std::move(nj));
            }
            tj["lanes"] = json::array();
            for (auto& l : t.lanes) {
                json lj = { { "param", l.param }, { "points", json::array() } };
                for (auto& pt : l.points) lj["points"].push_back({ pt.tick, pt.value });
                tj["lanes"].push_back(std::move(lj));
            }
            j["tracks"].push_back(std::move(tj));
        }
        return j.dump(2) + "\n";
    }

    Result<Project> project_from_json(const std::string& text, std::vector<Diagnostic>* warnings) {
        json j;
        try { j = json::parse(text); }
        catch (const json::exception& e) { return Result<Project>::fail(std::string("not valid JSON: ") + e.what()); }
        try {
            if (!j.is_object()) throw ParseError{ "project: top level must be an object" };
            if (get_or<std::string>(j, "format", "", "project") != kProjectFormatId) throw ParseError{ "not a Melovio project (format id is not 'melovio-project')" };
            const int ver = get_or<int>(j, "format_version", 0, "project");
            if (ver < 1) throw ParseError{ "project: missing or invalid format_version" };
            if (ver > kProjectFormatVersion) throw ParseError{ "project was written by a newer Melovio (format version " + std::to_string(ver) + ", this build reads up to " + std::to_string(kProjectFormatVersion) + ")" };

            Project p;
            if (j.contains("meta")) {
                const json& m = j["meta"];
                p.meta.title = get_or<std::string>(m, "title", "Untitled", "meta"); p.meta.composer = get_or<std::string>(m, "composer", "", "meta");
                p.meta.lyricist = get_or<std::string>(m, "lyricist", "", "meta"); p.meta.comment = get_or<std::string>(m, "comment", "", "meta");
                p.meta.created = get_or<std::string>(m, "created", "", "meta"); p.meta.modified = get_or<std::string>(m, "modified", "", "meta");
            }
            int ppq = kDefaultPPQ;
            if (j.contains("settings")) {
                ppq = get_or<int>(j["settings"], "ppq", kDefaultPPQ, "settings");
                p.sample_rate = get_or<int>(j["settings"], "sample_rate", 44100, "settings");
                if (ppq < 24 || ppq > 19200) throw ParseError{ "settings.ppq out of range" };
                if (p.sample_rate < 8000 || p.sample_rate > 192000) throw ParseError{ "settings.sample_rate out of range" };
            }
            std::vector<TempoChange> tempi; std::vector<TimeSigChange> sigs;
            if (j.contains("tempo_map")) {
                if (!j["tempo_map"].is_array()) throw ParseError{ "tempo_map: expected array" };
                for (auto& t : j["tempo_map"]) tempi.push_back({ get_or<Tick>(t, "tick", 0, "tempo_map[]"), get_or<double>(t, "bpm", 120.0, "tempo_map[]") });
            }
            if (j.contains("time_signatures")) {
                if (!j["time_signatures"].is_array()) throw ParseError{ "time_signatures: expected array" };
                for (auto& s : j["time_signatures"]) sigs.push_back({ get_or<int>(s, "bar", 0, "time_signatures[]"), get_or<int>(s, "numerator", 4, "time_signatures[]"), get_or<int>(s, "denominator", 4, "time_signatures[]") });
            }
            p.tempo_map = TempoMap(ppq, tempi, sigs);
            if (j.contains("loop")) { p.loop.enabled = get_or<bool>(j["loop"], "enabled", false, "loop"); p.loop.start = get_or<Tick>(j["loop"], "start", 0, "loop"); p.loop.end = get_or<Tick>(j["loop"], "end", 0, "loop"); }
            if (j.contains("voicebanks")) {
                if (!j["voicebanks"].is_array()) throw ParseError{ "voicebanks: expected array" };
                for (auto& v : j["voicebanks"]) {
                    VoicebankRef r; r.id = need(v, "id", "voicebanks[]").get<std::string>();
                    r.version = get_or<std::string>(v, "version", "", "voicebanks[]"); r.name = get_or<std::string>(v, "name", "", "voicebanks[]");
                    r.path_hint = get_or<std::string>(v, "path", "", "voicebanks[]");
                    if (!r.path_hint.empty() && is_absolute_utf8(r.path_hint)) {
                        // Hand-edited or foreign file: never follow absolute paths from a project.
                        if (warnings) warnings->push_back({ Severity::Warning, "project.absolute_path_ignored", "voicebank '" + r.id + "': absolute path in project ignored" });
                        r.path_hint.clear();
                    }
                    p.voicebanks.push_back(std::move(r));
                }
            }
            NoteId max_id = 0;
            if (j.contains("tracks")) {
                if (!j["tracks"].is_array()) throw ParseError{ "tracks: expected array" };
                int ti = 0;
                for (auto& tj : j["tracks"]) {
                    const std::string ctx = "tracks[" + std::to_string(ti++) + "]";
                    Track t;
                    t.id = get_or<std::string>(tj, "id", "", ctx); if (t.id.empty()) t.id = make_id("trk");
                    t.name = get_or<std::string>(tj, "name", "Vocal", ctx); t.voicebank_id = get_or<std::string>(tj, "voicebank", "", ctx);
                    t.persona = get_or<std::string>(tj, "persona", "", ctx); t.persona_amount = get_or<float>(tj, "persona_amount", 1.f, ctx);
                    t.style = params_from(tj.value("style", json()), ctx + ".style");
                    t.natural = get_or<float>(tj, "natural", 0.f, ctx); t.natural_take = get_or<int>(tj, "natural_take", 1, ctx);
                    t.auto_expression = get_or<bool>(tj, "auto_expression", false, ctx);
                    t.gain_db = get_or<float>(tj, "gain_db", 0.f, ctx); t.pan = std::clamp(get_or<float>(tj, "pan", 0.f, ctx), -1.f, 1.f);
                    t.mute = get_or<bool>(tj, "mute", false, ctx); t.solo = get_or<bool>(tj, "solo", false, ctx);
                    if (tj.contains("notes")) {
                        if (!tj["notes"].is_array()) throw ParseError{ ctx + ".notes: expected array" };
                        int ni = 0;
                        for (auto& nj : tj["notes"]) {
                            const std::string nctx = ctx + ".notes[" + std::to_string(ni++) + "]";
                            Note n;
                            n.id = get_or<NoteId>(nj, "id", 0, nctx);
                            n.tick = need(nj, "tick", nctx).get<Tick>(); n.length = need(nj, "length", nctx).get<Tick>(); n.pitch = need(nj, "pitch", nctx).get<int>();
                            if (n.tick < 0 || n.length <= 0) throw ParseError{ nctx + ": tick must be >= 0 and length > 0" };
                            if (n.pitch < 0 || n.pitch > 127) throw ParseError{ nctx + ".pitch: outside 0..127" };
                            n.lyric = get_or<std::string>(nj, "lyric", "", nctx); n.phonemes = get_or<std::string>(nj, "phonemes", "", nctx);
                            n.velocity = std::clamp(get_or<float>(nj, "velocity", 0.75f, nctx), 0.f, 1.f); n.offset_ms = get_or<double>(nj, "offset_ms", 0.0, nctx);
                            n.params = params_from(nj.value("params", json()), nctx + ".params");
                            max_id = std::max(max_id, n.id);
                            t.notes.push_back(std::move(n));
                        }
                    }
                    if (tj.contains("lanes")) {
                        if (!tj["lanes"].is_array()) throw ParseError{ ctx + ".lanes: expected array" };
                        for (auto& lj : tj["lanes"]) {
                            Lane l; l.param = need(lj, "param", ctx + ".lanes[]").get<std::string>();
                            if (!lj.contains("points")) throw ParseError{ ctx + ".lanes[]: missing 'points'" };
                            const json& pts = lj["points"];
                            for (auto& pt : pts) {
                                if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) throw ParseError{ ctx + ".lanes[" + l.param + "]: points must be [tick, value] pairs" };
                                l.points.push_back({ pt[0].get<Tick>(), pt[1].get<float>() });
                            }
                            std::stable_sort(l.points.begin(), l.points.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
                            t.lanes.push_back(std::move(l));
                        }
                    }
                    p.tracks.push_back(std::move(t));
                }
            }
            // Assign ids to notes without one, then enforce the invariants (sorted; overlaps are reported, not silently rewritten).
            p.next_note_id = std::max<NoteId>(get_or<NoteId>(j, "next_note_id", 1, "project"), max_id + 1);
            for (auto& t : p.tracks) { for (auto& n : t.notes) if (n.id == 0) n.id = p.alloc_note_id(); t.sort_notes(); }
            if (warnings) for (auto& d : validate_project(p)) if (d.severity != Severity::Info) warnings->push_back(d);
            return Result<Project>::success(std::move(p));
        }
        catch (const ParseError& e) { return Result<Project>::fail(e.msg); }
        catch (const json::exception& e) { return Result<Project>::fail(std::string("invalid project data: ") + e.what()); }
    }

    Status save_project(Project& p, const fs::path& path, std::vector<Diagnostic>* warnings) {
        p.meta.modified = iso_utc_now();
        if (p.meta.created.empty()) p.meta.created = p.meta.modified;
        return write_text_file_atomic(path, project_to_json(p, path.has_parent_path() ? path.parent_path() : fs::current_path(), warnings));
    }

    Result<Project> load_project(const fs::path& path, std::vector<Diagnostic>* warnings) {
        std::error_code ec;
        if (!fs::exists(path, ec)) return Result<Project>::fail("file not found: " + path_to_utf8(path));
        std::string err; std::string text = read_text_file(path, &err);
        if (!err.empty()) return Result<Project>::fail(err);
        if (text.empty()) return Result<Project>::fail("file is empty: " + path_to_utf8(path));
        return project_from_json(text, warnings);
    }

    fs::path AutosaveStore::file_for(const fs::path& project_path) const {
        const std::string key = project_path.empty() ? "untitled" : to_hex(fnv1a64(path_to_utf8(fs::absolute(project_path))));
        return dir_ / (key + ".autosave.json");
    }

    Status AutosaveStore::write(const Project& p, const fs::path& project_path) const {
        json j;
        j["original_path"] = project_path.empty() ? std::string() : path_to_utf8(fs::absolute(project_path));
        j["saved_at"] = iso_utc_now();
        j["project"] = json::parse(project_to_json(p, project_path.empty() ? fs::path() : project_path.parent_path()));
        return write_text_file_atomic(file_for(project_path), j.dump());
    }

    std::vector<AutosaveStore::Candidate> AutosaveStore::find_recoverable() const {
        std::vector<Candidate> out; std::error_code ec;
        if (!fs::is_directory(dir_, ec)) return out;
        for (auto& e : fs::directory_iterator(dir_, ec)) {
            if (e.path().filename().string().find(".autosave.json") == std::string::npos) continue;
            std::string err; std::string text = read_text_file(e.path(), &err);
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("project")) continue;   // corrupt autosave is ignored, never crashes startup
            Candidate c; c.autosave_file = e.path(); c.original_path = j.value("original_path", std::string());
            c.modified = j.value("saved_at", std::string());
            if (!c.original_path.empty()) {
                auto orig = path_from_utf8(c.original_path);
                if (fs::exists(orig, ec)) {
                    auto ot = fs::last_write_time(orig, ec), at = fs::last_write_time(e.path(), ec);
                    if (ot >= at) { fs::remove(e.path(), ec); continue; }   // the saved file is newer: nothing to recover
                }
            }
            out.push_back(std::move(c));
        }
        std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) { return a.modified > b.modified; });
        return out;
    }

    Result<Project> AutosaveStore::recover(const Candidate& c, std::vector<Diagnostic>* warnings) const {
        std::string err; std::string text = read_text_file(c.autosave_file, &err);
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.contains("project")) return Result<Project>::fail("autosave is corrupt");
        return project_from_json(j["project"].dump(), warnings);
    }
    void AutosaveStore::discard(const fs::path& project_path) const { std::error_code ec; fs::remove(file_for(project_path), ec); }
    void AutosaveStore::discard(const Candidate& c) const { std::error_code ec; fs::remove(c.autosave_file, ec); }
}
