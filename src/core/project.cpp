#include "project.hpp"
#include <algorithm>

namespace melovio {
    float Lane::value_at(Tick t) const {
        if (points.empty()) return 0.f;
        if (t <= points.front().tick) return points.front().value;
        if (t >= points.back().tick) return points.back().value;
        auto it = std::upper_bound(points.begin(), points.end(), t, [](Tick v, const AutomationPoint& p) { return v < p.tick; });
        const auto& b = *it; const auto& a = *(it - 1);
        const float f = static_cast<float>(t - a.tick) / static_cast<float>(std::max<Tick>(1, b.tick - a.tick));
        return a.value + (b.value - a.value) * f;
    }
    void Lane::set_point(Tick t, float v) {
        auto it = std::lower_bound(points.begin(), points.end(), t, [](const AutomationPoint& p, Tick x) { return p.tick < x; });
        if (it != points.end() && it->tick == t) it->value = v; else points.insert(it, { t, v });
    }
    void Lane::remove_range(Tick a, Tick b) {
        points.erase(std::remove_if(points.begin(), points.end(), [&](const AutomationPoint& p) { return p.tick >= a && p.tick <= b; }), points.end());
    }

    Note* Track::find_note(NoteId nid) { for (auto& n : notes) if (n.id == nid) return &n; return nullptr; }
    const Note* Track::find_note(NoteId nid) const { for (auto& n : notes) if (n.id == nid) return &n; return nullptr; }
    Lane* Track::find_lane(const std::string& p) { for (auto& l : lanes) if (l.param == p) return &l; return nullptr; }
    Lane& Track::ensure_lane(const std::string& p) { if (auto* l = find_lane(p)) return *l; lanes.push_back(Lane{ p, {} }); return lanes.back(); }
    void Track::sort_notes() { std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; }); }

    int Track::resolve_overlaps(const std::set<NoteId>& keep) {
        sort_notes();
        int removed = 0;
        // Pass 1: remove notes that are fully covered by (or start at the same tick as) a kept note.
        for (size_t i = 0; i < notes.size();) {
            bool kill = false;
            for (size_t j = 0; j < notes.size() && !kill; ++j) {
                if (i == j || !keep.count(notes[j].id) || keep.count(notes[i].id)) continue;
                const Note& k = notes[j]; const Note& n = notes[i];
                if (n.tick >= k.tick && n.end() <= k.end()) kill = true;
                else if (n.tick > k.tick && n.tick < k.end()) {
                    // n starts inside k: n loses its head (moved to k's end) if enough remains
                    if (n.end() > k.end()) { notes[i].length = n.end() - k.end(); notes[i].tick = k.end(); }
                    else kill = true;
                }
            }
            if (kill) { notes.erase(notes.begin() + static_cast<long>(i)); ++removed; } else ++i;
        }
        sort_notes();
        // Pass 2: trim earlier notes that run into the next one; drop duplicates at the same tick.
        for (size_t i = 0; i + 1 < notes.size();) {
            Note& a = notes[i]; Note& b = notes[i + 1];
            if (a.tick == b.tick) {
                const bool keep_a = keep.count(a.id) > 0 && !keep.count(b.id);
                notes.erase(notes.begin() + static_cast<long>(keep_a ? i + 1 : i)); ++removed; continue;
            }
            if (a.end() > b.tick) a.length = b.tick - a.tick;
            ++i;
        }
        return removed;
    }

    Tick Track::end_tick() const { Tick e = 0; for (auto& n : notes) e = std::max(e, n.end()); return e; }

    Project::Project() {
        meta.created = meta.modified = iso_utc_now();
    }

    Track& Project::add_track(const std::string& name, const std::string& voicebank_id) {
        Track t; t.id = make_id("trk"); t.name = name; t.voicebank_id = voicebank_id;
        tracks.push_back(std::move(t)); return tracks.back();
    }
    void Project::remove_track(const std::string& id) {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.id == id; }), tracks.end());
    }
    Track* Project::find_track(const std::string& id) { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }
    const Track* Project::find_track(const std::string& id) const { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }

    NoteId Project::add_note(Track& t, Note n) {
        if (n.id == 0) n.id = alloc_note_id();
        n.length = std::max<Tick>(1, n.length); n.tick = std::max<Tick>(0, n.tick);
        n.pitch = std::clamp(n.pitch, 0, 127);
        const NoteId id = n.id;
        t.notes.push_back(std::move(n));
        t.resolve_overlaps({ id });
        return id;
    }

    VoicebankRef* Project::find_voicebank_ref(const std::string& id) { for (auto& v : voicebanks) if (v.id == id) return &v; return nullptr; }
    const VoicebankRef* Project::find_voicebank_ref(const std::string& id) const { for (auto& v : voicebanks) if (v.id == id) return &v; return nullptr; }
    void Project::ensure_voicebank_ref(const std::string& id, const std::string& version, const std::string& name) {
        if (auto* r = find_voicebank_ref(id)) { if (!version.empty()) r->version = version; if (!name.empty()) r->name = name; return; }
        voicebanks.push_back({ id, version, name, "" });
    }
    Tick Project::end_tick() const { Tick e = 0; for (auto& t : tracks) e = std::max(e, t.end_tick()); return e; }

    std::vector<Diagnostic> validate_project(const Project& p) {
        std::vector<Diagnostic> d;
        auto add = [&](Severity s, const char* c, std::string m) { d.push_back({ s, c, std::move(m) }); };
        std::set<std::string> ids;
        for (auto& t : p.tracks) {
            if (!ids.insert(t.id).second) add(Severity::Error, "project.duplicate_track_id", "duplicate track id " + t.id);
            if (t.voicebank_id.empty()) add(Severity::Error, "project.track_no_voicebank", "track '" + t.name + "' has no voicebank");
            for (auto& kv : t.style) if (!find_param(kv.first) || !find_param(kv.first)->note_param) add(Severity::Warning, "project.unknown_param", "track '" + t.name + "': unsupported parameter '" + kv.first + "'");
            for (auto& l : t.lanes) { auto* pd = find_param(l.param); if (!pd || !pd->lane) add(Severity::Warning, "project.unknown_lane", "track '" + t.name + "': unsupported lane '" + l.param + "'"); }
            for (size_t i = 0; i < t.notes.size(); ++i) {
                const Note& n = t.notes[i];
                const std::string at = "track '" + t.name + "' note #" + std::to_string(i + 1) + ": ";
                if (n.length <= 0) add(Severity::Error, "note.length", at + "non-positive length");
                if (n.pitch < 0 || n.pitch > 127) add(Severity::Error, "note.pitch", at + "pitch outside 0..127");
                if (i + 1 < t.notes.size() && n.end() > t.notes[i + 1].tick) add(Severity::Error, "note.overlap", at + "overlaps the next note (voices are monophonic)");
                if (n.lyric.empty() && n.phonemes.empty()) add(Severity::Warning, "note.no_lyric", at + "no lyric");
                for (auto& kv : n.params) if (!find_param(kv.first) || !find_param(kv.first)->note_param) add(Severity::Warning, "note.unknown_param", at + "unsupported parameter '" + kv.first + "'");
            }
        }
        return d;
    }
}
