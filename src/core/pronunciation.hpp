// Japanese pronunciation layer: how written lyrics become the phoneme sequence the engine will sing.
//
// Analysis uses Wauvio-Vocal's own lyric parser (the same code the renderer runs), so what the editor shows is exactly what is
// synthesised. On top of that Melovio adds: syllable segmentation, phoneme notation round-tripping, detection of text the engine
// cannot pronounce (kanji, unknown symbols), per-note pronunciation overrides, particle correction, a user reading dictionary
// and a pluggable reading provider for kanji -> kana conversion.
//
// What this is NOT: a morphological analyser. Kanji cannot be read without a dictionary + grammar (MeCab/OpenJTalk class tools).
// Melovio reports kanji instead of guessing; supply kana, or plug a provider into set_reading_provider().
#pragma once
#include "util.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace melovio {
    struct PronunciationOptions {
        bool merge_long_vowels = true;   // "おう" inside one lyric becomes one long vowel (mirrors the voicebank's parse options)
        bool cute = false;
    };

    struct Phoneme {
        std::string symbol;              // "k", "a", "sh", "N" (moraic nasal), "Q" (sokuon), "-" (long vowel)
        enum class Kind { Consonant, Vowel, Special } kind = Kind::Vowel;
    };

    // One mora/syllable as the engine sees it (Japanese singing is mora-timed).
    struct Syllable {
        std::string source;              // the text it came from (e.g. "きゃ")
        std::vector<Phoneme> phonemes;   // e.g. {k, y?}... see phoneme_token
        std::string token;               // phoneme notation, e.g. "ky a", "N", "Q", "-"
        float weight = 1.f;              // share of the note's time relative to other syllables of the same note
        float consonant_scale = 1.f;
        bool long_vowel = false, geminate = false, nasal = false, palatal = false;
        bool representable = true;       // false: no phoneme notation exists (e.g. くぁ); edit the lyric instead
    };

    struct LyricAnalysis {
        std::vector<Syllable> syllables;
        std::vector<Diagnostic> diagnostics;
        std::string phoneme_text;        // canonical phoneme notation for the whole lyric, e.g. "ka N a"; "" if nothing
        bool from_override = false;      // true when analysed from an explicit phoneme string
        bool pronounceable = true;       // false if the engine would produce silence or drop text
    };

    // kanji -> kana. Return nullopt when the text is unknown.
    class IReadingProvider {
    public:
        virtual ~IReadingProvider() = default;
        virtual std::optional<std::string> reading(const std::string& text) const = 0;
    };

    // Simple user dictionary: "surface<TAB>reading" lines (UTF-8, '#' comments). Longest match wins.
    class UserDictionary : public IReadingProvider {
    public:
        void add(const std::string& surface, const std::string& reading) { map_[surface] = reading; }
        Status load(const fs::path& file);
        std::optional<std::string> reading(const std::string& text) const override;
        size_t size() const { return map_.size(); }
    private:
        std::map<std::string, std::string> map_;
    };

    class PronunciationEngine {
    public:
        void set_reading_provider(std::shared_ptr<const IReadingProvider> p) { provider_ = std::move(p); }

        // Written lyric (kana / romaji / "{phoneme notation}"). The reading provider, if any, is applied to kanji first.
        LyricAnalysis analyze(const std::string& lyric, const PronunciationOptions& opt = {}) const;
        // Explicit pronunciation in phoneme notation, with or without surrounding braces: "k a N@2 a*1.5".
        LyricAnalysis analyze_phonemes(const std::string& phonemes, const PronunciationOptions& opt = {}) const;
        // What to feed the engine for a note: its override if present, else its lyric.
        LyricAnalysis analyze_note(const std::string& lyric, const std::string& phoneme_override, const PronunciationOptions& opt = {}) const;

        // Text the engine receives for a note ("{k a}" braces added for overrides). Reading dictionary is applied. Never empty for
        // pronounceable input.
        std::string engine_lyric(const std::string& lyric, const std::string& phoneme_override) const;

    private:
        std::shared_ptr<const IReadingProvider> provider_;
        std::string apply_readings(const std::string& text, std::vector<Diagnostic>& diags) const;
    };

    bool contains_kanji(const std::string& utf8);
    // Splits typed lyrics into one unit per note. Whitespace, '|' ',' '/' separate explicitly; otherwise kana/romaji are split into morae
    // (きゃ and しゅ stay together, っ ん ー are their own units). {…} phoneme groups stay intact.
    std::vector<std::string> split_lyric_units(const std::string& text);
    // Particles read differently from how they are written: は -> wa, へ -> e, を -> o. Returns the phoneme override ("w a") or "".
    std::string particle_pronunciation(const std::string& lyric);
    // Hiragana <-> katakana helpers for display.
    std::string to_hiragana(const std::string& utf8);

    // Phoneme notation for one syllable with its weight/consonant-strength modifiers, e.g. "k a@2*1.5"-style ("k a@2" is written "k a@2").
    std::string syllable_token(const Syllable& s);
    // Phoneme notation for a mora list is produced by analyze(); this parses/validates user-typed notation.
    // Returns empty on success or the error message.
    std::string validate_phoneme_text(const std::string& phonemes);
}
