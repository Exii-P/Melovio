#include "test_util.hpp"
using namespace melovio; using namespace tu;

TEST_CASE("note and phoneme timing: ticks become seconds in the plan") {
    Project p = small_project(); p.tempo_map.set_tempi({ { 0, 120.0 } });
    PronunciationEngine pe; const Voicebank& vb = *bundled_library().find("yune");
    BuiltPlan b = build_track_plan(p, p.tracks[0], vb, pe);
    REQUIRE(b.plan.notes.size() == 3);
    const double origin = b.origin_sec;
    CHECK(origin == doctest::Approx(0.0));                                         // first note at 0: no room for a lead-in
    CHECK(b.plan.notes[1].start + origin == doctest::Approx(0.5));
    CHECK(b.plan.notes[1].dur == doctest::Approx(460.0 / 960.0));                  // 460 ticks at 120 bpm
    CHECK(b.plan.notes[2].midi == 72);
    CHECK(b.plan.notes[0].engine_lyric == "ら");
    // tempo change inside the phrase
    p.tempo_map.set_tempi({ { 0, 120.0 }, { 480, 60.0 } });
    b = build_track_plan(p, p.tracks[0], vb, pe);
    CHECK(b.plan.notes[1].start + b.origin_sec == doctest::Approx(0.5));
    CHECK(b.plan.notes[1].dur == doctest::Approx(460.0 / 480.0));                  // 460 ticks at 60 bpm
    CHECK(b.plan.notes[2].start + b.origin_sec == doctest::Approx(1.5));
}
TEST_CASE("synthesis input: parameters, overrides, offsets and lanes reach the plan") {
    Project p = small_project(); PronunciationEngine pe; const Voicebank& vb = *bundled_library().find("yune");
    Track& t = p.tracks[0];
    t.persona = "soft"; t.natural = 0.5f; t.style["tension"] = 0.7f; t.style["warp_drive"] = 1.f;                    // unsupported keys never reach the engine
    t.notes[0].velocity = 0.4f; t.notes[0].offset_ms = 100.0; t.notes[0].params["vibrato_depth"] = 0.9f; t.notes[0].params["vibrato_rate"] = 99.f;   // clamped
    t.notes[1].phonemes = "k a"; t.notes[2].lyric = "漢";
    t.ensure_lane("pitch").set_point(0, 0.f); t.ensure_lane("pitch").set_point(480, 2.f);
    t.ensure_lane("breathiness").set_point(0, 0.f);                                                                      // all-zero lane is not sent
    t.ensure_lane("reg").set_point(0, 0.5f);
    BuiltPlan b = build_track_plan(p, t, vb, pe);
    CHECK(b.plan.persona == "soft"); CHECK(b.plan.natural == doctest::Approx(0.5f));
    CHECK(b.plan.style.at("tension") == doctest::Approx(0.7f)); CHECK(b.plan.style.count("warp_drive") == 0);
    REQUIRE(b.plan.notes.size() == 2);                                                                                    // the kanji note was skipped...
    CHECK(b.skipped_notes == 1);
    bool reported = false; for (auto& d : b.diagnostics) if (d.code == "plan.note_skipped") reported = true; CHECK(reported);   // ...and said so
    CHECK(b.plan.notes[0].velocity == doctest::Approx(0.4f));
    CHECK(b.plan.notes[0].start + b.origin_sec == doctest::Approx(0.1));                                                 // +100 ms offset
    CHECK(b.plan.notes[0].style.at("vibrato_depth") == doctest::Approx(0.9f));
    CHECK(b.plan.notes[0].style.at("vibrato_rate") == doctest::Approx(10.f));
    CHECK(b.plan.notes[1].engine_lyric == "{k a}");
    bool has_pitch = false, has_reg = false, has_breath = false;
    for (auto& l : b.plan.lanes) { has_pitch |= l.name == "pitch"; has_reg |= l.name == "register"; has_breath |= l.name == "breathiness"; }
    CHECK(has_pitch); CHECK(has_reg); CHECK_FALSE(has_breath);
}
TEST_CASE("voicebank phonetic mappings are applied before synthesis") {
    Project p = small_project(); PronunciationEngine pe; Voicebank vb = *bundled_library().find("yune");
    vb.manifest.phonetic_mappings["ら"] = "r a@2";
    BuiltPlan b = build_track_plan(p, p.tracks[0], vb, pe);
    CHECK(b.plan.notes[0].engine_lyric == "{r a@2}");
    p.tracks[0].notes[0].phonemes = "d a";                                         // an explicit per-note pronunciation beats the mapping
    CHECK(build_track_plan(p, p.tracks[0], vb, pe).plan.notes[0].engine_lyric == "{d a}");
}
TEST_CASE("phrases split at long rests") {
    Project p; Track& t = p.add_track("a", "yune"); p.tempo_map.set_tempi({ { 0, 120.0 } });
    for (int i : { 0, 1, 2, 8, 9, 20 }) { Note n; n.tick = i * 480; n.length = 400; n.lyric = "あ"; p.add_note(t, n); }
    auto ph = split_phrases(t, p.tempo_map);                                      // 1 s = 2 beats: gaps > 1 s split
    REQUIRE(ph.size() == 3);
    CHECK(ph[0].first == 0); CHECK(ph[0].last == 2); CHECK(ph[1].first == 3); CHECK(ph[1].last == 4); CHECK(ph[2].first == 5);
}
TEST_CASE("preflight reports problems before rendering") {
    PronunciationEngine pe; Project p = small_project();
    p.tracks[0].notes[0].pitch = 20;                                              // far below the voice's hard range
    p.tracks[0].notes[1].lyric = "桜";
    auto d = preflight(p, banks(), pe);
    bool kanji = false, range = false;
    for (auto& x : d) { kanji |= x.code == "pronunciation.needs_reading"; range |= x.message.find("range") != std::string::npos; }
    CHECK(kanji); CHECK(range);
    auto d2 = preflight(p, {}, pe);
    bool missing = false; for (auto& x : d2) missing |= x.code == "preflight.voicebank_missing"; CHECK(missing);
}

TEST_CASE("integration: real Wauvio-Vocal renders the right pitches") {
    Project p = small_project(); PronunciationEngine pe;
    Renderer r(banks());
    RenderOptions o; o.sample_rate = 44100;
    auto m = r.render_track(p, p.tracks[0], o);
    REQUIRE(m); REQUIRE(m->size() > 44100);
    CHECK(rms_of(*m, 44100, 0.1, 1.4) > 0.01);                                     // audible
    CHECK(f0_of(*m, 44100, 0.2, 0.45) == doctest::Approx(440.0).epsilon(0.02));    // A4
    CHECK(f0_of(*m, 44100, 0.7, 0.95) == doctest::Approx(493.88).epsilon(0.02));   // B4
    CHECK(f0_of(*m, 44100, 1.2, 1.45) == doctest::Approx(523.25).epsilon(0.02));   // C5
}
TEST_CASE("integration: all four bundled voices sing, and sound different") {
    PronunciationEngine pe; double rms[4]; int i = 0;
    for (auto id : { "yune", "koharu", "reiha", "itsuki" }) {
        Project p = small_project(id); p.tracks[0].notes[0].pitch = p.tracks[0].notes[1].pitch = p.tracks[0].notes[2].pitch = 60;
        Renderer r(banks({ id })); RenderOptions o;
        auto m = r.render_track(p, p.tracks[0], o); INFO(id); REQUIRE(m);
        rms[i++] = rms_of(*m, 44100, 0.1, 1.3); CHECK(rms[i - 1] > 0.01);
    }
    Project a = small_project("yune"), b = small_project("itsuki");
    auto ya = Renderer(banks({ "yune" })).render_track(a, a.tracks[0], {}), yb = Renderer(banks({ "itsuki" })).render_track(b, b.tracks[0], {});
    REQUIRE(ya); REQUIRE(yb);
    size_t diff = 0; for (size_t k = 0; k < std::min(ya->size(), yb->size()); ++k) if (std::fabs((*ya)[k] - (*yb)[k]) > 1e-4f) ++diff;
    CHECK(diff > ya->size() / 2);
}
TEST_CASE("integration: parameters audibly change the output; lanes work") {
    Renderer r(banks()); RenderOptions o;
    Project base = small_project();
    auto ref = r.render_track(base, base.tracks[0], o); REQUIRE(ref);
    auto differs = [&](const Project& q) {
        auto m = Renderer(banks()).render_track(q, q.tracks[0], o); REQUIRE(m);
        double d = 0; for (size_t i = 0; i < std::min(m->size(), ref->size()); ++i) d += std::fabs((*m)[i] - (*ref)[i]); return d; };
    for (const char* k : { "breathiness", "brightness", "tension", "growl", "fry", "air", "tremor" }) { Project q = base; q.tracks[0].style[k] = 0.9f; INFO(k); CHECK(differs(q) > 1.0); }
    { Project q = base; q.tracks[0].style["formant_shift"] = 4.f; CHECK(differs(q) > 1.0); }
    { Project q = base; q.tracks[0].notes[0].params["vibrato_depth"] = 1.5f; CHECK(differs(q) > 0.5); }
    { Project q = base; q.tracks[0].ensure_lane("volume").set_point(0, -0.5f); auto m = Renderer(banks()).render_track(q, q.tracks[0], o); REQUIRE(m);
      CHECK(rms_of(*m, 44100, 0.1, 1.3) == doctest::Approx(rms_of(*ref, 44100, 0.1, 1.3) * 0.5).epsilon(0.1)); }
    { Project q = base; q.tracks[0].ensure_lane("pitch").set_point(0, 12.f);                                            // +1 octave
      auto m = Renderer(banks()).render_track(q, q.tracks[0], o); REQUIRE(m); CHECK(f0_of(*m, 44100, 0.2, 0.45, 300, 1500) == doctest::Approx(880.0).epsilon(0.03)); }
    { Project q = base; q.tracks[0].notes[1].offset_ms = 150.0;                                                          // moves audio later
      auto m = Renderer(banks()).render_track(q, q.tracks[0], o); REQUIRE(m); CHECK(rms_of(*m, 44100, 0.5, 0.6) < rms_of(*ref, 44100, 0.5, 0.6)); }
}
TEST_CASE("rendering is deterministic and the phrase cache works") {
    Project p = small_project(); Renderer r(banks()); RenderOptions o; o.threads = 1;
    auto a = r.render_track(p, p.tracks[0], o); REQUIRE(a);
    CHECK(r.cache().misses() == 1); CHECK(r.cache().entries() == 1);
    auto b = r.render_track(p, p.tracks[0], o); REQUIRE(b);
    CHECK(r.cache().hits() == 1);
    CHECK(*a == *b);
    Renderer fresh(banks()); auto c = fresh.render_track(p, p.tracks[0], o); REQUIRE(c); CHECK(*a == *c);               // same audio without the cache
    RenderOptions o8; o8.threads = 8; Renderer r8(banks()); auto d = r8.render_track(p, p.tracks[0], o8); REQUIRE(d); CHECK(*a == *d);   // and regardless of thread count
    // editing one phrase only re-renders that phrase
    Project q; Track& t = q.add_track("t", "yune"); q.tempo_map.set_tempi({ { 0, 120.0 } });
    for (int i : { 0, 1, 10, 11 }) { Note n; n.tick = i * 480; n.length = 400; n.lyric = "ら"; q.add_note(t, n); }
    Renderer rc(banks()); REQUIRE(rc.render_track(q, t, o)); CHECK(rc.cache().misses() == 2);
    q.tracks[0].notes[3].pitch = 74;
    REQUIRE(rc.render_track(q, q.tracks[0], o)); CHECK(rc.cache().misses() == 3); CHECK(rc.cache().hits() == 1);
}
TEST_CASE("rendering: mix, mute, solo, gain, pan, regions, cancellation") {
    Project p = small_project(); Track& t2 = p.add_track("Second", "koharu"); p.ensure_voicebank_ref("koharu", "1.0.0", "koharu");
    Note n; n.tick = 0; n.length = 900; n.pitch = 60; n.lyric = "あ"; p.add_note(t2, n);
    Renderer r(banks({ "yune", "koharu" })); RenderOptions o;
    auto both = r.render_mix(p, o); REQUIRE(both);
    p.tracks[1].mute = true; auto one = r.render_mix(p, o); REQUIRE(one);
    CHECK(both->peak() != doctest::Approx(one->peak()));
    p.tracks[1].mute = false; p.tracks[0].solo = true; auto solo = r.render_mix(p, o); REQUIRE(solo);
    CHECK(one->left == solo->left); CHECK(one->right == solo->right);                                                           // solo of track 1 == mute of track 2
    p.tracks[0].solo = false; p.tracks[1].mute = true; p.tracks[0].gain_db = -6.f;
    auto quiet = r.render_mix(p, o); REQUIRE(quiet);
    CHECK(quiet->peak() == doctest::Approx(one->peak() * 0.501).epsilon(0.01));
    p.tracks[0].gain_db = 0.f; p.tracks[0].pan = 1.f;
    auto right = r.render_mix(p, o); REQUIRE(right);
    CHECK(rms_of(right->left, 44100, 0, 1.5) < 1e-6); CHECK(rms_of(right->right, 44100, 0, 1.5) > 0.01);
    // region
    p.tracks[0].pan = 0.f; RenderOptions ro; ro.t0 = 0.5; ro.t1 = 1.0;
    auto reg = r.render_mix(p, ro); REQUIRE(reg);
    CHECK(reg->frames() == 22050); CHECK(reg->start_sec == doctest::Approx(0.5));
    CHECK(f0_of(reg->left, 44100, 0.05, 0.3) == doctest::Approx(493.88).epsilon(0.02));        // the second note, B4
    // cancellation
    CancelToken ct; ct.cancel();
    auto cancelled = r.render_mix(p, o, {}, &ct); REQUIRE_FALSE(cancelled); CHECK(cancelled.error == "cancelled");
    // errors
    p.tracks[0].voicebank_id = "ghost"; p.tracks[1].mute = false;
    auto missing = r.render_mix(p, o); REQUIRE_FALSE(missing); CHECK(missing.error.find("ghost") != std::string::npos);
    // progress reaches 1
    p.tracks[0].voicebank_id = "yune"; double last = 0; auto pr = r.render_mix(p, o, [&](double f, const std::string&) { last = std::max(last, f); }); REQUIRE(pr); CHECK(last == doctest::Approx(1.0));
}
TEST_CASE("rendering an empty project or track is not an error") {
    Project p; p.add_track("empty", "yune");
    Renderer r(banks()); auto m = r.render_mix(p, {}); REQUIRE(m); CHECK(m->peak() == 0.0);
}
