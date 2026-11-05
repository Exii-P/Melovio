#include "voicebank.hpp"
#include "pronunciation.hpp"
#include <algorithm>
#include <cmath>

namespace melovio {
    static const char* const kStandardUnits[] = {
        "あ","い","う","え","お","か","き","く","け","こ","が","ぎ","ぐ","げ","ご","さ","し","す","せ","そ","ざ","じ","ず","ぜ","ぞ",
        "た","ち","つ","て","と","だ","ぢ","づ","で","ど","な","に","ぬ","ね","の","は","ひ","ふ","へ","ほ","ば","び","ぶ","べ","ぼ","ぱ","ぴ","ぷ","ぺ","ぽ",
        "ま","み","む","め","も","や","ゆ","よ","ら","り","る","れ","ろ","わ","を",
        "きゃ","きゅ","きょ","ぎゃ","ぎゅ","ぎょ","しゃ","しゅ","しょ","じゃ","じゅ","じょ","ちゃ","ちゅ","ちょ","にゃ","にゅ","にょ",
        "ひゃ","ひゅ","ひょ","びゃ","びゅ","びょ","ぴゃ","ぴゅ","ぴょ","みゃ","みゅ","みょ","りゃ","りゅ","りょ",
        "ふぁ","ふぃ","ふぇ","ふぉ","てぃ","でぃ","とぅ","どぅ","うぃ","うぇ","うぉ","しぇ","じぇ","ちぇ","つぁ",
        "あん","あっ","あー" };

    CompletenessReport check_completeness(const Voicebank& vb, int sr) {
        CompletenessReport rep;
        if (!vb.profile_ok) { rep.diagnostics.push_back({ Severity::Error, "vb.profile_invalid", "profile cannot be loaded: " + vb.profile_error }); return rep; }
        PronunciationEngine pron;
        PronunciationOptions popt; popt.merge_long_vowels = vb.profile.parse_merge_long_vowels; popt.cute = vb.profile.parse_cute;
        const int mid = (vb.profile.range_low + vb.profile.range_high) / 2;

        auto run = [&](const std::string& unit, const std::string& lyric, int midi) {
            PhonemeCheck c; c.unit = unit; c.phonemes = pron.analyze(lyric, popt).phoneme_text;
            TrackPlan plan; plan.profile_path = path_to_utf8(vb.profile_path());
            PlanNote n; n.start = 0.05; n.dur = 0.45; n.midi = midi; n.velocity = 0.75f; n.engine_lyric = pron.engine_lyric(lyric, "");
            plan.notes.push_back(n);
            auto r = render_plan(plan, sr);
            if (!r) c.problem = r.error;
            else if (r->empty()) c.problem = "engine returned no audio";
            else {
                double peak = 0, sum = 0; bool bad = false;
                for (float x : *r) { if (!std::isfinite(x)) bad = true; peak = std::max(peak, static_cast<double>(std::fabs(x))); sum += static_cast<double>(x) * x; }
                c.peak = peak; c.rms = std::sqrt(sum / static_cast<double>(r->size()));
                if (bad) c.problem = "non-finite samples";
                else if (peak < 1e-3) c.problem = "silent";
                else if (peak > 4.0) c.problem = "extreme level (peak " + std::to_string(peak).substr(0, 5) + ")";
            }
            c.ok = c.problem.empty();
            rep.checks.push_back(c);
        };
        for (const char* u : kStandardUnits) run(u, u, mid);
        for (const char* v : { "あ", "い", "う", "え", "お" }) {
            run(std::string(v) + " @low", v, vb.profile.range_low);
            run(std::string(v) + " @high", v, vb.profile.range_high);
        }
        rep.total = rep.checks.size();
        for (auto& c : rep.checks) if (c.ok) ++rep.passed;
        if (rep.passed != rep.total) rep.diagnostics.push_back({ Severity::Error, "vb.incomplete", std::to_string(rep.total - rep.passed) + " of " + std::to_string(rep.total) + " units failed to synthesise" });
        if (!vb.manifest.recordings.empty()) rep.diagnostics.push_back({ Severity::Info, "vb.no_sample_check", "recordings are not checked: the engine does not use them" });
        return rep;
    }
}
