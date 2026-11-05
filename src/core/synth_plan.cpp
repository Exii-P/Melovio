#include "synth_plan.hpp"
#include <algorithm>
#include <cmath>

namespace melovio {
    static double note_start_sec(const Note& n, const TempoMap& tm) { return tm.tick_to_sec(n.tick) + n.offset_ms / 1000.0; }

    std::vector<PhraseSpan> split_phrases(const Track& t, const TempoMap& tm, double gap_sec) {
        std::vector<PhraseSpan> out;
        for (size_t i = 0; i < t.notes.size(); ++i) {
            const Note& n = t.notes[i];
            const double s = std::max(0.0, note_start_sec(n, tm)), e = tm.tick_to_sec(n.end());
            if (!out.empty()) {
                PhraseSpan& cur = out.back();
                if (s - cur.end_sec < gap_sec) { cur.last = i; cur.end_sec = std::max(cur.end_sec, e); continue; }
            }
            out.push_back({ i, i, s, e });
        }
        return out;
    }

    static void hash_str(uint64_t& h, const std::string& s) { h = fnv1a64(s, h); h = fnv1a64("\x1f", 1, h); }
    template <class T> static void hash_pod(uint64_t& h, const T& v) { h = fnv1a64(&v, sizeof v, h); }

    static void hash_plan(BuiltPlan& b, int sample_rate_independent) {
        (void)sample_rate_independent;
        uint64_t h = 1469598103934665603ull;
        const TrackPlan& p = b.plan;
        hash_str(h, p.profile_path);
        { std::string err; std::string prof = read_text_file(path_from_utf8(p.profile_path), &err); h = fnv1a64(prof, h); }   // edits to the .voice file invalidate the cache
        hash_str(h, p.persona); hash_pod(h, p.persona_amount); hash_pod(h, p.natural); hash_pod(h, p.natural_take); hash_pod(h, p.auto_expression); hash_pod(h, p.seed);
        for (auto& kv : p.style) { hash_str(h, kv.first); hash_pod(h, kv.second); }
        for (auto& n : p.notes) {
            hash_pod(h, n.start); hash_pod(h, n.dur); hash_pod(h, n.midi); hash_pod(h, n.velocity); hash_str(h, n.engine_lyric);
            for (auto& kv : n.style) { hash_str(h, kv.first); hash_pod(h, kv.second); }
        }
        for (auto& l : p.lanes) { hash_str(h, l.name); for (auto& c : l.curve) { hash_pod(h, c.first); hash_pod(h, c.second); } }
        b.hash = h;
    }

    BuiltPlan build_phrase_plan(const Project& proj, const Track& t, const Voicebank& vb, const PronunciationEngine& pron, const PhraseSpan& span) {
        BuiltPlan b;
        const TempoMap& tm = proj.tempo_map;
        TrackPlan& plan = b.plan;
        plan.profile_path = path_to_utf8(vb.profile_path());
        plan.persona = t.persona; plan.persona_amount = t.persona_amount;
        for (auto& kv : t.style) { const ParamDef* d = find_param(kv.first); if (d && d->note_param) plan.style[d->id] = std::clamp(kv.second, d->min, d->max); }
        plan.natural = t.natural; plan.natural_take = t.natural_take; plan.auto_expression = t.auto_expression;

        b.start_sec = span.start_sec; b.end_sec = span.end_sec;
        b.origin_sec = std::max(0.0, span.start_sec - kPhraseLeadSec);
        PronunciationOptions popt;
        if (vb.profile_ok) { popt.merge_long_vowels = vb.profile.parse_merge_long_vowels; popt.cute = vb.profile.parse_cute; }
        for (size_t i = span.first; i <= span.last && i < t.notes.size(); ++i) {
            const Note& n = t.notes[i];
            const std::string at = "track '" + t.name + "' note " + std::to_string(i + 1) + " (" + n.lyric + ")";
            std::string phon = n.phonemes;
            if (phon.empty()) {   // voicebank-level mapping: written lyric -> pronunciation
                auto m = vb.manifest.phonetic_mappings.find(n.lyric);
                if (m != vb.manifest.phonetic_mappings.end()) phon = m->second;
            }
            LyricAnalysis a = pron.analyze_note(n.lyric, phon, popt);
            for (auto& d : a.diagnostics) if (d.severity != Severity::Info) b.diagnostics.push_back({ d.severity, d.code, at + ": " + d.message });
            if (!a.pronounceable || a.syllables.empty()) {
                b.diagnostics.push_back({ Severity::Error, "plan.note_skipped", at + ": skipped (nothing to sing)" });
                ++b.skipped_notes; continue;
            }
            PlanNote pn;
            const double s = note_start_sec(n, tm), e = tm.tick_to_sec(n.end());
            pn.start = std::max(0.0, s) - b.origin_sec;
            pn.dur = std::max(0.02, e - tm.tick_to_sec(n.tick));
            // keep the voice monophonic even if the timing offset pushed this note over the next one
            if (i + 1 <= span.last && i + 1 < t.notes.size()) {
                const double next = std::max(0.0, note_start_sec(t.notes[i + 1], tm)) - b.origin_sec;
                if (pn.start + pn.dur > next) pn.dur = std::max(0.02, next - pn.start);
            }
            pn.midi = n.pitch; pn.velocity = std::clamp(n.velocity, 0.f, 1.f); pn.source_index = static_cast<int>(i);
            pn.engine_lyric = pron.engine_lyric(n.lyric, phon);
            for (auto& kv : n.params) { const ParamDef* d = find_param(kv.first); if (d && d->note_param) pn.style[d->id] = std::clamp(kv.second, d->min, d->max); }
            plan.notes.push_back(std::move(pn));
        }

        // Automation lanes: sample the tick-based lane into second-based curves covering this phrase.
        const double lane_t0 = b.origin_sec, lane_t1 = span.end_sec + 0.6;
        const Tick k0 = tm.sec_to_tick(lane_t0), k1 = tm.sec_to_tick(lane_t1);
        for (auto& l : t.lanes) {
            const ParamDef* d = find_param(l.param);
            if (!d || !d->lane || l.points.empty()) continue;
            PlanLane pl; pl.name = engine_lane_name(d->id);
            auto push = [&](Tick tk) {
                const double sec = tm.tick_to_sec(tk) - b.origin_sec;
                const double v = std::clamp(l.value_at(tk), d->min, d->max);
                if (!pl.curve.empty() && sec <= pl.curve.back().first) return;
                pl.curve.push_back({ sec, v });
            };
            push(k0);
            for (auto& pt : l.points) if (pt.tick > k0 && pt.tick < k1) push(pt.tick);
            push(k1);
            bool all_zero = true; for (auto& c : pl.curve) if (c.second != 0.0) all_zero = false;
            if (!all_zero) plan.lanes.push_back(std::move(pl));
        }
        hash_plan(b, 0);
        return b;
    }

    BuiltPlan build_track_plan(const Project& p, const Track& t, const Voicebank& vb, const PronunciationEngine& pron) {
        BuiltPlan all; bool first = true;
        for (const PhraseSpan& ph : split_phrases(t, p.tempo_map, 1e18)) {   // one giant phrase
            all = build_phrase_plan(p, t, vb, pron, ph); first = false;
        }
        if (first) { all.plan.profile_path = path_to_utf8(vb.profile_path()); }
        return all;
    }

    std::vector<Diagnostic> preflight(const Project& p, const std::map<std::string, Voicebank>& banks, const PronunciationEngine& pron) {
        std::vector<Diagnostic> out;
        for (auto& d : validate_project(p)) out.push_back(d);
        for (auto& t : p.tracks) {
            auto it = banks.find(t.voicebank_id);
            if (it == banks.end() || !it->second.usable()) {
                out.push_back({ Severity::Error, "preflight.voicebank_missing", "track '" + t.name + "': voicebank '" + t.voicebank_id + "' is missing or unusable" });
                continue;
            }
            const Voicebank& vb = it->second;
            if (!t.persona.empty()) {
                bool known = false; for (auto& n : vb.profile.personas) if (n == t.persona) known = true;
                if (!known) out.push_back({ Severity::Warning, "preflight.persona", "track '" + t.name + "': voicebank has no persona '" + t.persona + "'; it will be ignored" });
            }
            BuiltPlan bp = build_track_plan(p, t, vb, pron);
            for (auto& d : bp.diagnostics) out.push_back(d);
            if (!bp.plan.notes.empty()) for (auto& d : diagnose_plan(bp.plan)) out.push_back({ d.severity, d.code, "track '" + t.name + "': " + d.message });
        }
        return out;
    }
}
