#include "test_util.hpp"
using namespace melovio; using namespace tu;

TEST_CASE("japanese: kana to phonemes") {
    PronunciationEngine pe;
    auto a = pe.analyze("さくら");
    REQUIRE(a.syllables.size() == 3);
    CHECK(a.phoneme_text == "s a k u r a");
    CHECK(a.pronounceable);
    CHECK(pe.analyze("カタカナ").phoneme_text == "k a t a k a n a");              // katakana is read like hiragana
    CHECK(pe.analyze("しゃ").phoneme_text == "sh a");
    CHECK(pe.analyze("きょ").phoneme_text == "ky o");
    CHECK(pe.analyze("ち").phoneme_text == "ch i");
    CHECK(pe.analyze("つ").phoneme_text == "ts u");
    CHECK(pe.analyze("ふ").phoneme_text == "f u");
    CHECK(pe.analyze("じ").phoneme_text == "j i");
    CHECK(pe.analyze("を").phoneme_text == "o");                                  // を is /o/
    CHECK(pe.analyze("ん").phoneme_text == "N");
    CHECK(pe.analyze("っ").phoneme_text == "Q");
    CHECK(pe.analyze("ー").phoneme_text == "-");
}
TEST_CASE("japanese: long vowels and romaji") {
    PronunciationEngine pe;
    CHECK(pe.analyze("おう").phoneme_text == "o -");                              // merged into one long vowel
    PronunciationOptions no_merge; no_merge.merge_long_vowels = false;
    CHECK(pe.analyze("おう", no_merge).phoneme_text == "o u");
    CHECK(pe.analyze("sakura").phoneme_text == "s a k u r a");
    CHECK(pe.analyze("kyou").phoneme_text.find("ky o") == 0);
    auto a = pe.analyze("きゃ"); REQUIRE(a.syllables.size() == 1); CHECK(a.syllables[0].palatal);
}
TEST_CASE("japanese: kanji is reported, never silently dropped") {
    PronunciationEngine pe;
    auto a = pe.analyze("桜");
    CHECK_FALSE(a.pronounceable);
    REQUIRE_FALSE(a.diagnostics.empty());
    CHECK(a.diagnostics[0].code == "pronunciation.needs_reading");
    CHECK(contains_kanji("さ桜")); CHECK_FALSE(contains_kanji("さくら"));
    // mixed: the kana part is analysed, the problem is still flagged
    auto m = pe.analyze("さ桜"); CHECK_FALSE(m.pronounceable); CHECK(m.syllables.size() >= 1);
}
TEST_CASE("japanese: user dictionary supplies readings for kanji") {
    auto dict = std::make_shared<UserDictionary>(); dict->add("桜", "さくら"); dict->add("夢", "ゆめ");
    PronunciationEngine pe; pe.set_reading_provider(dict);
    auto a = pe.analyze("桜"); CHECK(a.pronounceable); CHECK(a.phoneme_text == "s a k u r a");
    CHECK(pe.analyze("夢桜").phoneme_text == "y u m e s a k u r a");
    CHECK(pe.engine_lyric("桜", "") == "さくら");
    TempDir d; { std::ofstream f(d / "dict.tsv"); f << "# comment\n空\tそら\n"; }
    UserDictionary u; REQUIRE(u.load(d / "dict.tsv")); CHECK(u.size() == 1); CHECK(*u.reading("空") == "そら");
    { std::ofstream f(d / "bad.tsv"); f << "no-tab-here\n"; }
    CHECK_FALSE(UserDictionary().load(d / "bad.tsv"));
}
TEST_CASE("japanese: unsupported characters are warned about") {
    PronunciationEngine pe;
    auto a = pe.analyze("さ★ら");
    bool warned = false; for (auto& d : a.diagnostics) if (d.code == "pronunciation.ignored_chars") warned = true;
    CHECK(warned);
    CHECK_FALSE(pe.analyze("★").pronounceable);
    CHECK_FALSE(pe.analyze("").pronounceable);
}
TEST_CASE("phoneme editing: notation parses, weights and consonant strength") {
    PronunciationEngine pe;
    auto a = pe.analyze_phonemes("{k a@2 n*1.5}");
    CHECK(a.from_override);
    REQUIRE(a.syllables.size() == 2);
    CHECK(a.syllables[0].weight == doctest::Approx(2.f));
    CHECK(a.syllables[1].nasal);
    CHECK(pe.analyze_phonemes("sh i").phoneme_text == "sh i");
    CHECK(pe.analyze_phonemes("ng a").phoneme_text == "ng a");
    CHECK(validate_phoneme_text("k a N Q -") == "");
    CHECK(validate_phoneme_text("zzz") != "");
    CHECK(validate_phoneme_text("") != "");
    CHECK(validate_phoneme_text("k a }") != "");
    auto bad = pe.analyze_phonemes("k a xyz");
    bool flagged = false; for (auto& d : bad.diagnostics) if (d.code == "pronunciation.bad_phonemes") flagged = true;
    CHECK(flagged);
    CHECK(pe.engine_lyric("は", "w a") == "{w a}");                               // override wins and is wrapped for the engine
    CHECK(pe.analyze_note("は", "w a").phoneme_text == "w a");
}
TEST_CASE("phoneme notation round-trips through the engine for the standard kana") {
    PronunciationEngine pe;
    const char* units[] = { "あ","か","が","さ","ざ","た","だ","な","は","ば","ぱ","ま","や","ら","わ","きゃ","しゅ","ちょ","にゃ","ひゅ","みょ","りゃ","ぎゃ","びょ","ぴゃ","ふぁ","ふぇ","うぃ","つぁ","に","ひ","し","ち","つ","ふ","じ","ん","っ" };
    for (auto u : units) {
        auto a = pe.analyze(u); auto b = pe.analyze_phonemes(a.phoneme_text);
        INFO("unit " << u << " -> '" << a.phoneme_text << "'");
        CHECK(a.pronounceable);
        CHECK(b.phoneme_text == a.phoneme_text);
        CHECK(b.syllables.size() == a.syllables.size());
        CHECK(a.syllables[0].representable);
    }
    // the sounds the notation cannot express are flagged, not mis-edited
    for (auto u : { "てぃ", "とぅ", "でぃ", "どぅ", "しぇ", "くぁ" }) CHECK_FALSE(pe.analyze(u).syllables[0].representable);
}
TEST_CASE("syllable segmentation and lyric distribution") {
    auto u = split_lyric_units("さくらさくら");
    REQUIRE(u.size() == 6); CHECK(u[2] == "ら");
    u = split_lyric_units("きゃ しゅ ー"); REQUIRE(u.size() == 3); CHECK(u[0] == "きゃ");
    u = split_lyric_units("さくら きらめく"); REQUIRE(u.size() == 2); CHECK(u[1] == "きらめく");   // explicit spaces mean one unit per note
    u = split_lyric_units("きゃっと"); REQUIRE(u.size() == 3); CHECK(u[1] == "っ");
    u = split_lyric_units("sakura"); REQUIRE(u.size() == 3); CHECK(u[0] == "sa");
    u = split_lyric_units("{k a} {s a}"); REQUIRE(u.size() == 2); CHECK(u[0] == "{k a}");
    CHECK(split_lyric_units("").empty());
}
TEST_CASE("particle pronunciation") {
    CHECK(particle_pronunciation("は") == "w a"); CHECK(particle_pronunciation("ハ") == "w a");
    CHECK(particle_pronunciation("へ") == "e"); CHECK(particle_pronunciation("を") == "o");
    CHECK(particle_pronunciation("か") == "");
    CHECK(to_hiragana("カタ") == "かた");
}
