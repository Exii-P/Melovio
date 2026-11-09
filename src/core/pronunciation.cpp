#include "pronunciation.hpp"
#include "wauvio_vocal/yune-v2/japanese.hpp"
#include <algorithm>
#include <fstream>
#include <cstring>
#include <sstream>

namespace melovio {
    namespace wv = wauvio::vocal;

    namespace {
        std::vector<uint32_t> decode(const std::string& s) {
            std::vector<uint32_t> o; size_t i = 0; uint32_t cp;
            while (wv::jp_detail::next_cp(s, i, cp)) o.push_back(cp);
            return o;
        }
        std::string encode(const std::vector<uint32_t>& v) { std::string s; for (auto c : v) s += wv::jp_detail::cp_to_utf8(c); return s; }

        bool is_kanji_cp(uint32_t c) { return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF) || c == 0x3005; }
        bool is_separator_cp(uint32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '|' || c == ',' || c == '/' || c == 0x3000 || c == 0x3001 || c == 0x3002; }
        bool is_kana_or_prolong(uint32_t c) {
            uint32_t h = wv::jp_detail::to_hira(c);
            if (c == 0x30FC || c == U'〜' || h == U'っ' || h == U'ん') return true;
            wv::Vowel v;
            if (wv::jp_detail::is_small_ya(h, v) || wv::jp_detail::is_small_vowel(h, v)) return true;
            return wv::jp_detail::cv_table().count(h) > 0;
        }

        const char* cons_symbol(wv::Surf s) {
            using wv::Surf;
            switch (s) {
                case Surf::K: return "k"; case Surf::G: return "g"; case Surf::T: return "t"; case Surf::D: return "d";
                case Surf::P: return "p"; case Surf::B: return "b"; case Surf::TS: return "ts"; case Surf::CH: return "ch";
                case Surf::J: return "j"; case Surf::S: return "s"; case Surf::SH: return "sh"; case Surf::Z: return "z";
                case Surf::H: return "h"; case Surf::HY: return "hy"; case Surf::FU: return "f"; case Surf::N: return "n";
                case Surf::NY: return "ny"; case Surf::M: return "m"; case Surf::R: return "r"; case Surf::Y: return "y";
                case Surf::W: return "w"; case Surf::NG: return "ng";
                default: return "";
            }
        }
        const char* vowel_symbol(wv::Vowel v) { static const char* t[] = { "a", "i", "u", "e", "o" }; return t[static_cast<int>(v)]; }

        std::string fmt_num(float f) { std::ostringstream o; o << f; return o.str(); }

        Syllable describe(const wv::Mora& m) {
            Syllable s;
            s.source = m.text; s.weight = m.weight; s.consonant_scale = m.cons_scale;
            s.long_vowel = m.longv; s.geminate = m.geminate; s.nasal = m.nasal; s.palatal = m.palatal;
            auto add = [&](const std::string& sym, Phoneme::Kind k) { s.phonemes.push_back({ sym, k }); };
            if (m.geminate) add("Q", Phoneme::Kind::Special);
            else if (m.nasal) add("N", Phoneme::Kind::Special);
            else if (m.longv) add("-", Phoneme::Kind::Special);
            else {
                const wv::SurfaceMora sm = wv::surface(m);
                std::string c = cons_symbol(sm.surf);
                // y-glide written as a yo-on (きゃ) on a surface that is not inherently palatal: ky, gy, ny ... as the engine's notation expects.
                if (!m.palatal && (sm.surf == wv::Surf::NY || sm.surf == wv::Surf::HY)) c.resize(1);   // に ひ: palatal comes from the vowel i
                if (sm.palatal && !c.empty()) {
                    const bool inherent = sm.surf == wv::Surf::SH || sm.surf == wv::Surf::CH || sm.surf == wv::Surf::J || sm.surf == wv::Surf::NY || sm.surf == wv::Surf::HY || sm.surf == wv::Surf::Y;
                    if (!inherent && c.size() == 1) c += "y";
                }
                if (sm.labial) s.representable = false;      // くぁ: w-glide has no notation in the engine's phoneme syntax
                if (m.force == wv::Mora::Force::TI || m.force == wv::Mora::Force::DI || m.force == wv::Mora::Force::S || m.force == wv::Mora::Force::Z) {
                    // てぃ とぅ でぃ すぃ ずぃ: plain consonant + i/u; romaji notation "ti" would be re-read as ち, so mark non-representable
                    s.representable = false;
                }
                if ((m.force == wv::Mora::Force::SH || m.force == wv::Mora::Force::J || m.force == wv::Mora::Force::CH) && m.v == wv::Vowel::E) s.representable = false;   // しぇ じぇ ちぇ
                if (!c.empty()) add(c, Phoneme::Kind::Consonant);
                add(vowel_symbol(m.v), Phoneme::Kind::Vowel);
            }
            for (size_t i = 0; i < s.phonemes.size(); ++i) { if (i) s.token += ' '; s.token += s.phonemes[i].symbol; }
            if (s.weight != 1.f) s.token += "@" + fmt_num(s.weight);
            if (s.consonant_scale != 1.f) s.token += "*" + fmt_num(s.consonant_scale);
            return s;
        }

        LyricAnalysis from_morae(const std::vector<wv::Mora>& ms, bool override_) {
            LyricAnalysis a; a.from_override = override_;
            for (auto& m : ms) a.syllables.push_back(describe(m));
            for (size_t i = 0; i < a.syllables.size(); ++i) {
                if (i) a.phoneme_text += ' ';
                a.phoneme_text += a.syllables[i].token;
            }
            return a;
        }

        std::string unsupported_chars(const std::string& text) {
            std::string bad;
            for (uint32_t c : decode(text)) {
                if (c < 0x80 || is_separator_cp(c) || is_kana_or_prolong(c)) continue;
                if (is_kanji_cp(c)) continue;      // reported separately
                if (c == U'！' || c == U'？' || c == U'・' || c == U'…' || c == U'「' || c == U'」') continue;   // punctuation: harmless, ignored
                bad += wv::jp_detail::cp_to_utf8(c);
            }
            return bad;
        }
    }

    Status UserDictionary::load(const fs::path& file) {
        std::string err; std::string t = read_text_file(file, &err);
        if (!err.empty()) return Status::fail(err);
        std::istringstream in(t); std::string line; int n = 0;
        while (std::getline(in, line)) {
            ++n;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) return Status::fail("line " + std::to_string(n) + ": expected 'surface<TAB>reading'");
            map_[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return Status::success();
    }
    std::optional<std::string> UserDictionary::reading(const std::string& text) const {
        auto it = map_.find(text); if (it == map_.end()) return std::nullopt; return it->second;
    }

    bool contains_kanji(const std::string& s) { for (uint32_t c : decode(s)) if (is_kanji_cp(c)) return true; return false; }

    std::string to_hiragana(const std::string& s) {
        std::vector<uint32_t> o; for (uint32_t c : decode(s)) o.push_back(c == 0x30FC ? c : wv::jp_detail::to_hira(c)); return encode(o);
    }

    std::string particle_pronunciation(const std::string& lyric) {
        std::string h = to_hiragana(lyric);
        if (h == "は") return "w a";
        if (h == "へ") return "e";
        if (h == "を") return "o";
        return "";
    }

    std::vector<std::string> split_lyric_units(const std::string& text) {
        std::vector<std::string> out;
        // 1) explicit separators / braces
        std::vector<std::string> chunks; std::string cur; int depth = 0;
        for (uint32_t c : decode(text)) {
            if (c == '{') ++depth;
            if (c == '}') depth = std::max(0, depth - 1);
            if (depth == 0 && is_separator_cp(c)) { if (!cur.empty()) { chunks.push_back(cur); cur.clear(); } continue; }
            cur += wv::jp_detail::cp_to_utf8(c);
        }
        if (!cur.empty()) chunks.push_back(cur);
        const bool explicit_split = chunks.size() > 1;
        for (auto& ch : chunks) {
            if (ch.front() == '{' || explicit_split) {
                if (ch.front() == '{' || !explicit_split) { out.push_back(ch); continue; }
            }
            // 2) one unit per mora inside a chunk
            wv::ParseOptions po; po.merge_long_vowels = false;
            bool ascii = true; for (unsigned char b : ch) if (b >= 0x80) { ascii = false; break; }
            if (explicit_split && !ascii && !contains_kanji(ch) && ch.size() > 0) {
                // an explicitly separated token stays one unit even if it holds several kana ("さくら きらめく" -> 2 units): the user decided
                out.push_back(ch); continue;
            }
            if (explicit_split) { out.push_back(ch); continue; }
            if (contains_kanji(ch)) { out.push_back(ch); continue; }
            auto ms = ascii ? wv::parse_romaji(ch, po) : wv::parse_kana(ch, po);
            if (ms.empty()) { out.push_back(ch); continue; }
            for (auto& m : ms) if (!m.text.empty()) out.push_back(m.text);
        }
        return out;
    }

    std::string syllable_token(const Syllable& s) {
        std::string t;
        for (size_t i = 0; i < s.phonemes.size(); ++i) { if (i) t += ' '; t += s.phonemes[i].symbol; }
        if (s.weight != 1.f) t += "@" + fmt_num(s.weight);
        if (s.consonant_scale != 1.f) t += "*" + fmt_num(s.consonant_scale);
        return t;
    }

    std::string validate_phoneme_text(const std::string& ph) {
        std::string t = ph;
        { const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t"); if (a == std::string::npos) return "empty pronunciation"; t = t.substr(a, b - a + 1); }
        const bool open = t.front() == '{', close = t.back() == '}';
        if (open != close) return "unbalanced braces";
        if (open) t = t.substr(1, t.size() - 2);
        if (t.find_first_of("{}") != std::string::npos) return "unbalanced braces";
        { const size_t a = t.find_first_not_of(" \t"); if (a == std::string::npos) return "empty pronunciation"; t = t.substr(a); }
        wv::ParseOptions po;
        auto ms = wv::parse_phonemes(t, po);
        // The engine silently skips tokens it does not know; count tokens to catch that.
        if (ms.empty()) return "no recognisable phonemes (use e.g. 'k a', 'sh i', 'N', 'Q', '-')";
        // Tokens that join (consonant + vowel) make fewer morae than tokens; unknown tokens make fewer morae than expected. Re-render and compare.
        std::string canon; for (size_t i = 0; i < ms.size(); ++i) { if (i) canon += ' '; canon += describe(ms[i]).token; }
        std::istringstream is(t); std::string tk; std::string bad;
        static const char* known[] = { "k","g","s","z","t","d","n","h","f","b","p","m","r","y","w","v","l","sh","ch","ts","j","ky","gy","ny","hy","by","py","my","ry","ng","N","Q","-","~" };
        while (is >> tk) {
            size_t p = tk.find_first_of("*@"); std::string core = p == std::string::npos ? tk : tk.substr(0, p);
            if (core.empty()) continue;
            bool ok = false;
            for (const char* k : known) if (core == k) ok = true;
            if (!ok) { // vowel-initial or CV romaji: must parse to at least one mora with the same text
                auto m2 = wv::parse_romaji(core); ok = !m2.empty();
                std::string rebuilt; for (auto& m : m2) rebuilt += m.text;
                std::string lc; for (char c : core) lc += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ok && rebuilt.size() < lc.size() && lc != "nn") ok = false;
            }
            if (!ok) bad += (bad.empty() ? "" : ", ") + core;
        }
        if (!bad.empty()) return "unknown phoneme(s): " + bad;
        return "";
    }

    std::string PronunciationEngine::apply_readings(const std::string& text, std::vector<Diagnostic>& diags) const {
        if (!provider_ || !contains_kanji(text)) return text;
        // Greedy longest-match over the kanji runs.
        std::vector<uint32_t> cps = decode(text); std::string out;
        for (size_t i = 0; i < cps.size();) {
            if (!is_kanji_cp(cps[i])) { out += wv::jp_detail::cp_to_utf8(cps[i]); ++i; continue; }
            size_t j = cps.size(); bool found = false;
            for (; j > i; --j) {
                std::string sub = encode(std::vector<uint32_t>(cps.begin() + static_cast<long>(i), cps.begin() + static_cast<long>(j)));
                if (auto r = provider_->reading(sub)) { out += *r; found = true; break; }
            }
            if (found) i = j; else { out += wv::jp_detail::cp_to_utf8(cps[i]); ++i; }
        }
        (void)diags;
        return out;
    }

    LyricAnalysis PronunciationEngine::analyze(const std::string& lyric, const PronunciationOptions& opt) const {
        LyricAnalysis a;
        if (lyric.empty()) { a.pronounceable = false; a.diagnostics.push_back({ Severity::Warning, "pronunciation.empty", "no lyric" }); return a; }
        std::vector<Diagnostic> readings_diags;
        const std::string text = apply_readings(lyric, readings_diags);
        wv::ParseOptions po; po.merge_long_vowels = opt.merge_long_vowels; po.cute = opt.cute;
        const size_t br = text.find_first_not_of(" \t");
        if (br != std::string::npos && text[br] == '{') {
            const size_t close = text.rfind('}');
            std::string inner = close != std::string::npos && close > br ? text.substr(br + 1, close - br - 1) : text.substr(br + 1);
            a = analyze_phonemes(inner, opt); a.from_override = false; return a;
        }
        auto ms = (std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x80; })) ? wv::parse_romaji(text, po) : wv::parse_lyric(text, po);
        a = from_morae(ms, false);
        if (contains_kanji(text)) {
            a.diagnostics.push_back({ Severity::Error, "pronunciation.needs_reading",
                "contains kanji, which the engine cannot read. Type the reading in kana, give the note a pronunciation, or add the word to a user dictionary." });
            a.pronounceable = false;
        }
        const std::string bad = unsupported_chars(text);
        if (!bad.empty()) a.diagnostics.push_back({ Severity::Warning, "pronunciation.ignored_chars", "these characters are ignored by the engine: " + bad });
        if (a.syllables.empty()) {
            a.pronounceable = false;
            if (!contains_kanji(text)) a.diagnostics.push_back({ Severity::Error, "pronunciation.silent", "no pronounceable sounds found in '" + lyric + "'" });
        }
        for (auto& s : a.syllables) if (!s.representable) {
            a.diagnostics.push_back({ Severity::Info, "pronunciation.no_notation", "'" + s.source + "' has no phoneme notation; edit the lyric rather than the phonemes" }); break;
        }
        return a;
    }

    LyricAnalysis PronunciationEngine::analyze_phonemes(const std::string& phonemes, const PronunciationOptions& opt) const {
        LyricAnalysis a;
        const std::string err = validate_phoneme_text(phonemes);
        wv::ParseOptions po; po.merge_long_vowels = opt.merge_long_vowels; po.cute = opt.cute;
        std::string t = phonemes;
        { const size_t x = t.find_first_not_of(" \t{"), y = t.find_last_not_of(" \t}"); t = x == std::string::npos ? "" : t.substr(x, y - x + 1); }
        a = from_morae(wv::parse_phonemes(t, po), true);
        if (!err.empty()) { a.pronounceable = a.pronounceable && !a.syllables.empty(); a.diagnostics.push_back({ Severity::Error, "pronunciation.bad_phonemes", err }); if (a.syllables.empty()) a.pronounceable = false; }
        return a;
    }

    LyricAnalysis PronunciationEngine::analyze_note(const std::string& lyric, const std::string& ov, const PronunciationOptions& opt) const {
        return ov.empty() ? analyze(lyric, opt) : analyze_phonemes(ov, opt);
    }

    std::string PronunciationEngine::engine_lyric(const std::string& lyric, const std::string& ov) const {
        if (!ov.empty()) { std::string t = ov; const size_t x = t.find_first_not_of(" \t{"), y = t.find_last_not_of(" \t}"); t = x == std::string::npos ? "" : t.substr(x, y - x + 1); return "{" + t + "}"; }
        std::vector<Diagnostic> d; return apply_readings(lyric, d);
    }

    std::vector<std::string> parse_lyric_sheet(const std::string& text, LyricSheetMode mode, bool skip_headers) {
        static const char* kDrop[] = { "！", "？", "。", "、", "…", "‥", "「", "」", "『", "』", "（", "）", "・", "，", "．", "〜", "～", "♪", "♡", "☆", "!", "?", ",", ".", "~", "(", ")", "\"" };
        std::vector<std::string> out;
        std::istringstream lines(text); std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t a = 0;
            while (a < line.size()) {
                if (line[a] == ' ' || line[a] == '\t') ++a;
                else if (line.compare(a, 3, "\xE3\x80\x80") == 0) a += 3;      // U+3000
                else break;
            }
            if (a >= line.size()) continue;
            const std::string body = line.substr(a);
            if (skip_headers && (body[0] == '[' || body.compare(0, 3, "【") == 0)) {
                const size_t close = body.find_last_of("]】");
                if (close != std::string::npos && close + 1 >= body.size() - 2) continue;      // the whole line is a header
            }
            std::string clean;
            for (size_t i = 0; i < body.size();) {
                if (skip_headers && body[i] == '[') { const size_t c = body.find(']', i); if (c != std::string::npos) { i = c + 1; continue; } }
                bool dropped = false;
                for (const char* d : kDrop) { const size_t n = std::strlen(d); if (body.compare(i, n, d) == 0) { clean += ' '; i += n; dropped = true; break; } }
                if (dropped) continue;
                clean += body[i++];
            }
            std::vector<std::string> words; std::string cur; int depth = 0;
            for (size_t i = 0; i < clean.size(); ++i) {
                const char c = clean[i];
                const bool ws = c == ' ' || c == '\t' || (static_cast<unsigned char>(c) == 0xE3 && i + 2 < clean.size() && static_cast<unsigned char>(clean[i + 1]) == 0x80 && static_cast<unsigned char>(clean[i + 2]) == 0x80);
                if (c == '{') ++depth;
                if (c == '}' && depth > 0) --depth;
                if (ws && depth == 0) { if (!cur.empty()) words.push_back(cur); cur.clear(); if (static_cast<unsigned char>(c) == 0xE3) i += 2; }
                else cur += c;
            }
            if (!cur.empty()) words.push_back(cur);
            for (auto& w : words) {
                if (mode == LyricSheetMode::Words) out.push_back(w);
                else for (auto& u : split_lyric_units(w)) out.push_back(u);
            }
        }
        return out;
    }
}
