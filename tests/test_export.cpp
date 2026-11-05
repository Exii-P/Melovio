#include "test_util.hpp"
using namespace melovio; using namespace tu;

TEST_CASE("export configuration validation") {
    Project p = small_project(); ExportSettings s;
    CHECK(validate_export_settings(s, p).empty());
    auto bad = [&](auto mut) { ExportSettings x; mut(x); return has_errors(validate_export_settings(x, p)); };
    CHECK(bad([](ExportSettings& x) { x.sample_rate = 12345; }));
    CHECK(bad([](ExportSettings& x) { x.bit_depth = 8; }));
    CHECK(bad([](ExportSettings& x) { x.channels = 6; }));
    CHECK(bad([](ExportSettings& x) { x.tail_sec = -1; }));
    CHECK(bad([](ExportSettings& x) { x.normalize = true; x.normalize_peak_db = 3; }));
    CHECK(bad([](ExportSettings& x) { x.scope = ExportScope::Region; x.region_start = 100; x.region_end = 100; }));
    CHECK(bad([](ExportSettings& x) { x.scope = ExportScope::Track; x.track_id = "nope"; }));
    CHECK(has_errors(validate_export_settings(s, Project())));                       // empty project
    for (int sr : kExportSampleRates) { ExportSettings x; x.sample_rate = sr; CHECK(validate_export_settings(x, p).empty()); }
}
TEST_CASE("wav encoding: 16/24/32f, mono/stereo, round trip") {
    std::vector<float> sig; for (int i = 0; i < 2000; ++i) sig.push_back(0.5f * std::sin(static_cast<float>(i) * 0.05f));
    for (int bits : { 16, 24, 32 }) for (int ch : { 1, 2 }) {
        WavFormat f; f.bit_depth = bits; f.channels = ch; f.sample_rate = 48000; f.dither = false;
        auto bytes = encode_wav(sig, f); REQUIRE(bytes);
        CHECK(bytes->size() == 44 + sig.size() * static_cast<size_t>(bits / 8));
        auto d = decode_wav(*bytes); REQUIRE(d);
        CHECK(d->format.sample_rate == 48000); CHECK(d->format.channels == ch); CHECK(d->format.bit_depth == bits);
        REQUIRE(d->samples.size() == sig.size());
        const double tol = bits == 16 ? 1.0 / 32000 : bits == 24 ? 1e-6 : 0.0;
        for (size_t i = 0; i < sig.size(); i += 37) CHECK(std::fabs(d->samples[i] - sig[i]) <= tol + 1e-9);
    }
    WavFormat bad; bad.bit_depth = 12; CHECK_FALSE(encode_wav(sig, bad));
    bad = WavFormat(); bad.channels = 2; CHECK_FALSE(encode_wav(std::vector<float>(3, 0.f), bad));                  // odd sample count for stereo
    std::vector<float> loud = { 3.f, -3.f, NAN, 0.f }; WavFormat f; f.channels = 2; f.dither = false;
    auto d = decode_wav(*encode_wav(loud, f)); REQUIRE(d); CHECK(d->samples[0] == doctest::Approx(1.f).epsilon(1e-3)); CHECK(d->samples[1] == doctest::Approx(-1.f).epsilon(1e-3)); CHECK(d->samples[2] == 0.f);
    CHECK_FALSE(decode_wav({ 1, 2, 3 }));
}
TEST_CASE("export: full project to a real WAV file") {
    TempDir d; Project p = small_project(); Renderer r(banks());
    ExportSettings s; s.sample_rate = 48000; s.bit_depth = 24; s.channels = 2; s.tail_sec = 1.0;
    std::vector<double> prog;
    auto res = export_wav(p, r, s, d / "out" / "song.wav", [&](double f, const std::string&) { prog.push_back(f); });
    REQUIRE(res);
    CHECK(fs::exists(d / "out" / "song.wav")); CHECK_FALSE(fs::exists(d / "out" / "song.wav.part"));
    auto w = read_wav_file(d / "out" / "song.wav"); REQUIRE(w);
    CHECK(w->format.sample_rate == 48000); CHECK(w->format.bit_depth == 24); CHECK(w->format.channels == 2);
    CHECK(res->duration_sec == doctest::Approx(1.5 + 1.0 + 0.0).epsilon(0.02));                                  // 3 notes of 0.5 s (last ends at 1.4792) + 1 s tail
    CHECK(res->frames == w->frames());
    CHECK(res->peak > 0.05); CHECK_FALSE(res->clipped);
    REQUIRE_FALSE(prog.empty()); CHECK(prog.back() == doctest::Approx(1.0));
    // mono export sums the channels
    s.channels = 1; s.bit_depth = 16;
    REQUIRE(export_wav(p, r, s, d / "mono.wav")); auto m = read_wav_file(d / "mono.wav"); REQUIRE(m); CHECK(m->format.channels == 1);
    // normalisation
    s.normalize = true; s.normalize_peak_db = -6.0; s.bit_depth = 32;
    auto n = export_wav(p, r, s, d / "norm.wav"); REQUIRE(n); auto nw = read_wav_file(d / "norm.wav"); REQUIRE(nw);
    float pk = 0; for (float x : nw->samples) pk = std::max(pk, std::fabs(x)); CHECK(pk == doctest::Approx(0.501f).epsilon(0.01));
}
TEST_CASE("export: region, single track, errors, cancellation") {
    TempDir d; Project p = small_project(); Track& t2 = p.add_track("b", "koharu"); p.ensure_voicebank_ref("koharu", "1.0.0", "koharu");
    Note n; n.length = 960; n.pitch = 60; n.lyric = "あ"; p.add_note(t2, n);
    Renderer r(banks({ "yune", "koharu" }));
    ExportSettings s; s.scope = ExportScope::Region; s.region_start = 480; s.region_end = 960;                       // second beat only: 0.5 s
    auto reg = export_wav(p, r, s, d / "reg.wav"); REQUIRE(reg); CHECK(reg->duration_sec == doctest::Approx(0.5).epsilon(0.01));
    s = ExportSettings(); s.scope = ExportScope::Track; s.track_id = p.tracks[1].id; p.tracks[1].mute = true;        // muted tracks can still be exported on their own
    auto one = export_wav(p, r, s, d / "t.wav"); REQUIRE(one); CHECK(one->peak > 0.05);
    CHECK_FALSE(export_wav(p, r, [] { ExportSettings x; x.sample_rate = 1; return x; }(), d / "bad.wav"));
    CHECK_FALSE(fs::exists(d / "bad.wav"));
    CancelToken ct; ct.cancel();
    auto c = export_wav(p, r, ExportSettings(), d / "cancel.wav", {}, &ct); REQUIRE_FALSE(c);
    CHECK_FALSE(fs::exists(d / "cancel.wav")); CHECK_FALSE(fs::exists(d / "cancel.wav.part"));                         // a cancelled export leaves nothing behind
    // unwritable destination: a file where a directory must be
    { std::ofstream(d / "blocker") << "x"; }
    auto bad = export_wav(p, r, ExportSettings(), d / "blocker" / "x.wav"); CHECK_FALSE(bad);
    // missing voicebank during export is a clean error
    Renderer none({}); CHECK_FALSE(export_wav(p, none, ExportSettings(), d / "none.wav"));
}
TEST_CASE("export: clipping is reported, not hidden") {
    TempDir d; Project p = small_project(); p.tracks[0].gain_db = 40.f; Renderer r(banks());
    ExportSettings s; s.bit_depth = 16;
    auto res = export_wav(p, r, s, d / "loud.wav"); REQUIRE(res);
    CHECK(res->clipped);
    bool warned = false; for (auto& x : res->diagnostics) warned |= x.code == "export.clipping"; CHECK(warned);
}
