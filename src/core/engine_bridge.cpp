#include "engine_bridge.hpp"
#include "wauvio_vocal_v2.hpp"
#include <cmath>

namespace melovio {
    namespace wv = wauvio::vocal;

    namespace {
        Status load_vp(const fs::path& file, wv::VoiceProfile& out) {
            // Profiles override a complete base (Yune), exactly like the engine's own make_voice(): a partial .voice file inherits the rest.
            out = wv::voice_yune();
            std::string err;
            if (!wv::load_profile(path_to_utf8(file), out, &err)) return Status::fail(err);
            return Status::success();
        }

        void fill_params(const wv::VoiceProfile& p, ProfileParams& o) {
            using namespace wv::profile_detail;
            o.id = p.id; o.display = p.display; o.language = p.language; o.notes = p.notes; o.model_name = p.model.name;
            o.natural_amount = p.natural_amount;
            o.range_low = p.model.range_low; o.range_high = p.model.range_high; o.hard_low = p.model.hard_low; o.hard_high = p.model.hard_high;
            for (int i = 0; i < 5; ++i) {
                const auto& t = p.model.vowel[i];
                o.vowel[i] = { t.f1, t.f2, t.f3, t.b1, t.b2, t.b3, t.gain, t.f1_track, t.f2_track };
            }
            for (const MF& m : model_floats()) o.model[m.name] = p.model.*(m.p);
            for (const TF& t : tune_floats()) o.tune[t.name] = p.model.tune.*(t.p);
            for (const std::string& k : style_names()) o.style[k] = p.style.*(wv::style_field(k));
            o.parse_merge_long_vowels = p.parse.merge_long_vowels; o.parse_cute = p.parse.cute;
            o.personas = p.personas.names();
        }

        void apply_params(const ProfileParams& o, wv::VoiceProfile& p) {
            using namespace wv::profile_detail;
            p.id = o.id; p.display = o.display; p.language = o.language; p.notes = o.notes; p.model.name = o.model_name;
            p.natural_amount = o.natural_amount;
            p.parse.merge_long_vowels = o.parse_merge_long_vowels; p.parse.cute = o.parse_cute;
            p.model.range_low = o.range_low; p.model.range_high = o.range_high; p.model.hard_low = o.hard_low; p.model.hard_high = o.hard_high;
            for (int i = 0; i < 5; ++i) {
                auto& t = p.model.vowel[i]; const auto& v = o.vowel[i];
                t.f1 = v.f1; t.f2 = v.f2; t.f3 = v.f3; t.b1 = v.b1; t.b2 = v.b2; t.b3 = v.b3; t.gain = v.gain; t.f1_track = v.f1_track; t.f2_track = v.f2_track;
            }
            for (const MF& m : model_floats()) { auto it = o.model.find(m.name); if (it != o.model.end()) p.model.*(m.p) = it->second; }
            for (const TF& t : tune_floats()) { auto it = o.tune.find(t.name); if (it != o.tune.end()) p.model.tune.*(t.p) = it->second; }
            for (const std::string& k : style_names()) { auto it = o.style.find(k); if (it != o.style.end()) p.style.*(wv::style_field(k)) = it->second; }
        }

        // Builds a configured Singer from a plan. Order matters: persona first (it moves many Style fields), then explicit
        // track overrides, then notes (each note copies the Singer's Style at creation time).
        wv::Singer build_singer(const TrackPlan& plan, const wv::VoiceProfile& prof) {
            wv::Singer s(prof);
            s.set_tempo(120.0);
            if (plan.seed) s.seed(plan.seed);
            if (!plan.persona.empty() && s.personas().find(plan.persona)) s.persona(plan.persona, plan.persona_amount);
            for (auto& kv : plan.style) if (float wv::Style::* m = wv::style_field(kv.first)) s.style().*m = kv.second;
            if (plan.natural > 0.f) s.natural(plan.natural, static_cast<uint32_t>(std::max(1, plan.natural_take)));
            if (plan.auto_expression) s.auto_expression(true);
            for (auto& n : plan.notes) {
                wv::NoteRef r = s.note_at(n.start, n.midi, n.dur, n.engine_lyric, n.velocity);
                for (auto& kv : n.style) if (float wv::Style::* m = wv::style_field(kv.first)) r.style().*m = kv.second;
            }
            for (auto& l : plan.lanes) {
                wv::Curve c; for (auto& p : l.curve) c.push_back(p);
                s.lane(l.name, c);
            }
            return s;
        }
    }

    Result<ProfileParams> read_profile(const fs::path& file) {
        std::error_code ec;
        if (!fs::exists(file, ec)) return Result<ProfileParams>::fail("profile not found: " + path_to_utf8(file));
        wv::VoiceProfile p;
        Status st = load_vp(file, p);
        if (!st) return Result<ProfileParams>::fail(st.message);
        ProfileParams o; fill_params(p, o);
        return Result<ProfileParams>::success(std::move(o));
    }

    Status write_profile(const ProfileParams& params, const fs::path& file) {
        wv::VoiceProfile p = wv::voice_yune();
        std::error_code ec;
        if (fs::exists(file, ec)) { Status st = load_vp(file, p); if (!st) return st; }
        apply_params(params, p);
        return write_text_file_atomic(file, wv::profile_to_text(p));
    }

    std::vector<std::string> engine_builtin_voice_ids() { return wv::voice_ids(); }

    Status export_builtin_voice(const std::string& id, const fs::path& out) {
        wv::VoiceProfile p; std::string err;
        bool found = false; for (auto& v : wv::voice_ids()) if (v == id) found = true;
        if (!found || !wv::make_voice(id, p, &err)) return Status::fail("no built-in voice '" + id + "'");
        return write_text_file_atomic(out, wv::profile_to_text(p));
    }

    EngineInfo engine_info() { return { "Wauvio-Vocal", "v2", "parametric formant synthesiser (Klatt-style cascade/parallel, LF glottal source)" }; }

    Result<std::vector<float>> render_plan(const TrackPlan& plan, int sample_rate) {
        try {
            wv::VoiceProfile prof; Status st = load_vp(path_from_utf8(plan.profile_path), prof);
            if (!st) return Result<std::vector<float>>::fail("voice profile: " + st.message);
            wv::Singer s = build_singer(plan, prof);
            if (s.size() == 0) return Result<std::vector<float>>::success({});
            wauvio::Buffer b = s.render(sample_rate);
            for (float& x : b) if (!std::isfinite(x)) x = 0.f;
            return Result<std::vector<float>>::success(std::vector<float>(b.begin(), b.end()));
        } catch (const std::exception& e) {
            return Result<std::vector<float>>::fail(std::string("engine error: ") + e.what());
        }
    }

    std::vector<Diagnostic> diagnose_plan(const TrackPlan& plan) {
        std::vector<Diagnostic> out;
        try {
            wv::VoiceProfile prof; Status st = load_vp(path_from_utf8(plan.profile_path), prof);
            if (!st) { out.push_back({ Severity::Error, "engine.profile", st.message }); return out; }
            wv::Singer s = build_singer(plan, prof);
            for (auto& m : s.diagnostics()) out.push_back({ Severity::Warning, "engine.diagnostic", m });
        } catch (const std::exception& e) { out.push_back({ Severity::Error, "engine.exception", e.what() }); }
        return out;
    }
}