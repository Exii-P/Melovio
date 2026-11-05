#include "test_util.hpp"
using namespace melovio; using namespace tu;

TEST_CASE("project creation") {
    Project p;
    CHECK(p.tracks.empty());
    CHECK(p.tempo_map.ppq() == kDefaultPPQ);
    Track& t = p.add_track("Vocal", "yune");
    CHECK(!t.id.empty());
    CHECK(p.find_track(t.id) == &t);
    CHECK(p.end_tick() == 0);
}

TEST_CASE("notes stay monophonic: overlap resolution") {
    Project p; Track& t = p.add_track("a", "yune");
    Note a; a.tick = 0; a.length = 960; a.lyric = "あ"; const NoteId ia = p.add_note(t, a);
    Note b; b.tick = 480; b.length = 480; b.lyric = "い"; p.add_note(t, b);       // new note wins: earlier is shortened
    REQUIRE(t.notes.size() == 2);
    CHECK(t.find_note(ia)->length == 480);
    Note c; c.tick = 0; c.length = 2000; c.lyric = "う"; const NoteId ic = p.add_note(t, c);   // covers everything
    CHECK(t.notes.size() == 1);
    CHECK(t.notes[0].id == ic);
    CHECK(validate_project(p).empty());
}

TEST_CASE("automation lane interpolation") {
    Lane l; l.param = "volume";
    l.set_point(100, 0.f); l.set_point(300, 1.f); l.set_point(100, -1.f);      // replace point at 100
    CHECK(l.points.size() == 2);
    CHECK(l.value_at(50) == doctest::Approx(-1.f));                         // holds first value
    CHECK(l.value_at(200) == doctest::Approx(0.f));
    CHECK(l.value_at(999) == doctest::Approx(1.f));
    l.remove_range(0, 150); CHECK(l.points.size() == 1);
}

TEST_CASE("project serialisation round trip") {
    Project p = small_project();
    p.meta.title = "日本語タイトル"; p.meta.composer = "me";
    p.tempo_map.set_tempi({ { 0, 100.0 }, { 960, 80.0 } });
    p.tempo_map.set_signatures({ { 0, 3, 4 } });
    p.loop = { true, 480, 1440 };
    Track& t = p.tracks[0];
    t.persona = "soft"; t.persona_amount = 0.5f; t.natural = 0.4f; t.gain_db = -3.f; t.pan = 0.25f; t.mute = true;
    t.style["breathiness"] = 0.3f;
    t.notes[1].phonemes = "r a@2"; t.notes[1].offset_ms = -12.5; t.notes[1].params["vibrato_depth"] = 0.8f;
    t.ensure_lane("pitch").set_point(0, 0.f); t.ensure_lane("pitch").set_point(480, 1.5f);
    const std::string json1 = project_to_json(p);
    std::vector<Diagnostic> w;
    auto q = project_from_json(json1, &w);
    REQUIRE(q);
    CHECK(w.empty());
    CHECK(q->meta.title == "日本語タイトル");
    CHECK(q->tempo_map.tempi().size() == 2);
    CHECK(q->tempo_map.signatures()[0].numerator == 3);
    CHECK(q->loop.enabled); CHECK(q->loop.start == 480); CHECK(q->loop.end == 1440);
    const Track& u = q->tracks[0];
    CHECK(u.persona == "soft"); CHECK(u.persona_amount == doctest::Approx(0.5f)); CHECK(u.mute); CHECK(u.gain_db == doctest::Approx(-3.f));
    CHECK(u.style.at("breathiness") == doctest::Approx(0.3f));
    REQUIRE(u.notes.size() == 3);
    CHECK(u.notes[1].phonemes == "r a@2"); CHECK(u.notes[1].offset_ms == doctest::Approx(-12.5));
    CHECK(u.notes[1].params.at("vibrato_depth") == doctest::Approx(0.8f));
    CHECK(u.notes[0].id == p.tracks[0].notes[0].id);
    REQUIRE(u.lanes.size() == 1); CHECK(u.lanes[0].points.size() == 2);
    CHECK(q->next_note_id == p.next_note_id);
    CHECK(project_to_json(*q) == json1);                                     // stable: serialising again gives identical text
}

TEST_CASE("project save and load on disk") {
    TempDir d;
    Project p = small_project();
    REQUIRE(save_project(p, d / "song.mvp"));
    CHECK_FALSE(fs::exists(d / "song.mvp.tmp"));                              // atomic write leaves no temp file
    auto q = load_project(d / "song.mvp");
    REQUIRE(q);
    CHECK(q->tracks[0].notes.size() == 3);
    CHECK(q->meta.modified == p.meta.modified);
}

TEST_CASE("projects never contain absolute paths") {
    TempDir d; fs::create_directories(d / "banks" / "x");
    Project p = small_project();
    p.voicebanks[0].path_hint = path_to_utf8(d / "banks" / "x");              // absolute, inside the project's directory
    std::vector<Diagnostic> w;
    std::string js = project_to_json(p, d.path, &w);
    CHECK(js.find(d.path.string()) == std::string::npos);
    auto q = project_from_json(js); REQUIRE(q);
    CHECK(q->voicebanks[0].path_hint == "banks/x");
    // an absolute path that cannot be relativised is dropped with a warning, not written
    p.voicebanks[0].path_hint = "/definitely/elsewhere";
    std::vector<Diagnostic> w2; js = project_to_json(p, d.path / "sub", &w2);
    { auto back = project_from_json(js); REQUIRE(back); CHECK_FALSE(is_absolute_utf8(back->voicebanks[0].path_hint)); }   // stored relative (or dropped), never absolute
    // and a hand-edited project with an absolute path is not trusted
    std::string bad = project_to_json(small_project()); const std::string key = "\"version\": \"1.0.0\"";
    bad.replace(bad.find(key), key.size(), key + ", \"path\": \"/etc\"");
    std::vector<Diagnostic> w3; auto r = project_from_json(bad, &w3); REQUIRE(r);
    CHECK(r->voicebanks[0].path_hint.empty()); CHECK_FALSE(w3.empty());
}

TEST_CASE("invalid project files are rejected with a useful message") {
    CHECK_FALSE(project_from_json("not json at all"));
    CHECK(project_from_json("not json at all").error.find("JSON") != std::string::npos);
    CHECK_FALSE(project_from_json("[]"));
    CHECK_FALSE(project_from_json(R"({"format":"something-else","format_version":1})"));
    auto newer = project_from_json(R"({"format":"melovio-project","format_version":99})");
    REQUIRE_FALSE(newer); CHECK(newer.error.find("newer") != std::string::npos);
    CHECK_FALSE(project_from_json(R"({"format":"melovio-project"})"));                                         // no version
    auto bad_note = project_from_json(R"({"format":"melovio-project","format_version":1,"tracks":[{"notes":[{"tick":0,"length":0,"pitch":60}]}]})");
    REQUIRE_FALSE(bad_note); CHECK(bad_note.error.find("tracks[0].notes[0]") != std::string::npos);
    CHECK_FALSE(project_from_json(R"({"format":"melovio-project","format_version":1,"tracks":[{"notes":[{"tick":0,"length":10,"pitch":300}]}]})"));
    CHECK_FALSE(project_from_json(R"({"format":"melovio-project","format_version":1,"tracks":[{"notes":[{"tick":"x","length":10,"pitch":60}]}]})"));
    CHECK_FALSE(project_from_json(R"({"format":"melovio-project","format_version":1,"settings":{"ppq":1}})"));
    CHECK_FALSE(project_from_json(R"({"format":"melovio-project","format_version":1,"tracks":[{"style":{"x":"high"}}]})"));
    TempDir d;
    CHECK_FALSE(load_project(d / "missing.mvp"));
    { std::ofstream(d / "empty.mvp"); }
    CHECK_FALSE(load_project(d / "empty.mvp"));
    { std::ofstream f(d / "trunc.mvp"); f << project_to_json(small_project()).substr(0, 200); }
    CHECK_FALSE(load_project(d / "trunc.mvp"));                                                               // truncated file
}

TEST_CASE("unknown keys are ignored; unsupported parameters are reported") {
    std::string js = project_to_json(small_project());
    js.replace(js.rfind('}'), 1, ", \"future_feature\": {\"x\": 1} }");
    CHECK(project_from_json(js));
    Project p = small_project(); p.tracks[0].notes[0].params["warp_drive"] = 1.f;
    auto d = validate_project(p);
    REQUIRE_FALSE(d.empty()); CHECK(d[0].code == "note.unknown_param");
}

TEST_CASE("autosave and recovery") {
    TempDir d; AutosaveStore store(d / "autosave");
    Project p = small_project(); p.meta.title = "unsaved work";
    const fs::path proj = d / "song.mvp";
    REQUIRE(save_project(p, proj));
    p.tracks[0].notes[0].lyric = "あ";                                          // edit after the last save
    REQUIRE(store.write(p, proj));
    // autosave is newer than the saved file -> recoverable
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(store.write(p, proj));
    auto c = store.find_recoverable();
    REQUIRE(c.size() == 1);
    auto r = store.recover(c[0]); REQUIRE(r);
    CHECK(r->tracks[0].notes[0].lyric == "あ");
    // after a clean save the autosave is discarded and nothing is offered
    store.discard(proj);
    CHECK(store.find_recoverable().empty());
    // a never-saved project is recoverable too
    REQUIRE(store.write(p, {}));
    c = store.find_recoverable(); REQUIRE(c.size() == 1); CHECK(c[0].original_path.empty());
    // a corrupt autosave must never break start-up
    { std::ofstream f(d / "autosave" / "garbage.autosave.json"); f << "{ not json"; }
    CHECK(store.find_recoverable().size() == 1);
    // if the real file is newer than the autosave there is nothing to recover
    store.discard(fs::path());
    REQUIRE(store.write(p, proj));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(save_project(p, proj));
    CHECK(store.find_recoverable().empty());
}

TEST_CASE("undo and redo") {
    Project p = small_project(); History h;
    h.record(p, "move"); p.tracks[0].notes[0].pitch = 80;
    h.record(p, "lyric"); p.tracks[0].notes[0].lyric = "き";
    CHECK(h.can_undo()); CHECK(h.undo_label() == "lyric");
    REQUIRE(h.undo(p)); CHECK(p.tracks[0].notes[0].lyric == "ら"); CHECK(p.tracks[0].notes[0].pitch == 80);
    REQUIRE(h.undo(p)); CHECK(p.tracks[0].notes[0].pitch == 69);
    CHECK_FALSE(h.undo(p));
    REQUIRE(h.redo(p)); CHECK(p.tracks[0].notes[0].pitch == 80);
    h.record(p, "new edit");                                                   // a new edit clears the redo stack
    CHECK_FALSE(h.can_redo());
    // coalescing: one drag = one undo step
    History g; Project q = small_project();
    for (int i = 0; i < 10; ++i) { g.record(q, "drag", "drag-1"); q.tracks[0].notes[0].tick += 10; }
    REQUIRE(g.undo(q)); CHECK(q.tracks[0].notes[0].tick == 0); CHECK_FALSE(g.can_undo());
}
