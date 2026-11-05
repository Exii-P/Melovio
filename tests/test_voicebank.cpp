#include "test_util.hpp"
using namespace melovio; using namespace tu;

static void copy_bank(const std::string& id, const fs::path& to) {
    fs::create_directories(to);
    fs::copy(fs::path(MELOVIO_BUNDLED_VOICEBANKS) / id, to, fs::copy_options::recursive);
}
static void edit_manifest(const fs::path& dir, const std::function<void(nlohmann::json&)>& f) {
    std::string e; auto j = nlohmann::json::parse(read_text_file(dir / "voicebank.json", &e)); f(j);
    std::ofstream(dir / "voicebank.json") << j.dump(2);
}

TEST_CASE("voicebank discovery: bundled voicebanks") {
    auto& lib = bundled_library();
    REQUIRE(lib.all().size() >= 4);
    for (auto id : { "yune", "koharu", "reiha", "itsuki" }) {
        const Voicebank* v = lib.find(id); INFO(id); REQUIRE(v); CHECK(v->usable()); CHECK(v->bundled); CHECK(v->profile_ok);
        CHECK(v->manifest.recordings.empty());                                  // parametric: no invented samples
        CHECK_FALSE(v->manifest.completeness.empty());
    }
    CHECK(lib.find("nobody") == nullptr);
}
TEST_CASE("voicebank loading: metadata and engine profile") {
    const Voicebank* y = bundled_library().find("yune"); REQUIRE(y);
    CHECK(y->manifest.native_name == "夢音");
    CHECK(y->profile.range_low == 52); CHECK(y->profile.range_high == 88);
    CHECK(y->profile.vowel[0].f1 == doctest::Approx(880.f));                    // values come from the real .voice file
    CHECK_FALSE(y->profile.personas.empty());
    const Voicebank* k = bundled_library().find("koharu"); REQUIRE(k);
    CHECK(k->profile.vowel[0].f1 == doctest::Approx(862.f));
    CHECK(k->profile.range_low == 50);
}
TEST_CASE("voicebank manifest: round trip and errors") {
    const Voicebank* y = bundled_library().find("yune"); REQUIRE(y);
    auto m = manifest_from_json(manifest_to_json(y->manifest)); REQUIRE(m);
    CHECK(m->id == "yune"); CHECK(m->profile == "voice.voice"); CHECK(m->kind == "parametric-formant");
    CHECK_FALSE(manifest_from_json("{"));
    CHECK_FALSE(manifest_from_json(R"({"format":"x","format_version":1})"));
    CHECK_FALSE(manifest_from_json(R"({"format":"melovio-voicebank"})"));
    auto newer = manifest_from_json(R"({"format":"melovio-voicebank","format_version":42})");
    REQUIRE_FALSE(newer); CHECK(newer.error.find("newer") != std::string::npos);
}
TEST_CASE("voicebank compatibility validation") {
    TempDir d;
    auto has = [](const Voicebank& v, const char* code) { for (auto& x : v.diagnostics) if (x.code == code) return true; return false; };
    auto load = [&](const char* name, const std::function<void(nlohmann::json&)>& f) {
        copy_bank("yune", d / name); edit_manifest(d / name, f); auto r = load_voicebank(d / name); REQUIRE(r); return *r; };

    CHECK(load("ok", [](auto&) {}).usable());
    CHECK(has(load("a", [](auto& j) { j["id"] = "Bad Id!"; }), "vb.id_syntax"));
    CHECK(has(load("b", [](auto& j) { j["version"] = "banana"; }), "vb.version"));
    CHECK(has(load("c", [](auto& j) { j["languages"] = nlohmann::json::array({ "en" }); }), "vb.language"));
    CHECK(has(load("d", [](auto& j) { j["synthesis"]["kind"] = "sample-based"; }), "vb.kind"));
    CHECK(has(load("e", [](auto& j) { j["synthesis"]["engine"] = "other"; }), "vb.engine"));
    CHECK(has(load("f", [](auto& j) { j["synthesis"]["engine_version"] = "v9"; }), "vb.engine_version"));
    CHECK(has(load("g", [](auto& j) { j["compatibility"]["min_melovio"] = "99.0.0"; }), "vb.too_new"));
    CHECK(has(load("h", [](auto& j) { j["synthesis"]["profile"] = "../../etc/passwd"; }), "vb.profile_path"));
    CHECK(has(load("i", [](auto& j) { j["synthesis"]["profile"] = "nope.voice"; }), "vb.profile_invalid"));
    CHECK(has(load("j", [](auto& j) { j["phonetics"]["recordings"] = nlohmann::json::array({ { { "phoneme", "a" }, { "file", "samples/a.wav" } } }); }), "vb.recording_missing"));
    CHECK_FALSE(load("k", [](auto& j) { j["synthesis"]["kind"] = "sample-based"; }).usable());
    auto lic = load("l", [](auto& j) { j["license"] = ""; }); CHECK(has(lic, "vb.license_missing")); CHECK(lic.usable());    // warning only
    // a broken .voice file reports the engine's line-numbered error
    copy_bank("yune", d / "m"); { std::ofstream f(d / "m" / "voice.voice", std::ios::app); f << "frobnicate 3\n"; }
    auto m = load_voicebank(d / "m"); REQUIRE(m); CHECK_FALSE(m->usable());
    CHECK(m->profile_error.find("line ") != std::string::npos);                       // the engine's own parser error, with a line number
    CHECK(m->profile_error.find("frobnicate") != std::string::npos);
    CHECK_FALSE(load_voicebank(d / "does-not-exist"));
}
TEST_CASE("voicebank completeness check runs the real engine") {
    const Voicebank* y = bundled_library().find("yune"); REQUIRE(y);
    auto rep = check_completeness(*y);
    CHECK(rep.total > 100);
    CHECK(rep.passed == rep.total);
    CHECK(rep.complete());
    for (auto& c : rep.checks) { INFO(c.unit); CHECK(c.peak > 0.001); CHECK(c.ok); }
    // sabotage the profile so the vowels cannot be produced -> the check notices (zero output gain => silence)
    TempDir d; copy_bank("yune", d / "mute");
    { std::string e; std::string t = read_text_file(d / "mute" / "voice.voice", &e); const size_t a = t.find("tune out_scale"), b = t.find('\n', a);
      t.replace(a, b - a, "tune out_scale 0"); std::ofstream(d / "mute" / "voice.voice") << t; }
    auto bad = load_voicebank(d / "mute"); REQUIRE(bad); REQUIRE(bad->usable());
    auto rep2 = check_completeness(*bad);
    CHECK_FALSE(rep2.complete());
    CHECK(rep2.passed < rep2.total);
}
TEST_CASE("voicebank install, versions, removal") {
    TempDir d; VoicebankLibrary lib; lib.set_user_dir(d / "user"); lib.add_search_dir(fs::path(MELOVIO_BUNDLED_VOICEBANKS), true); lib.refresh();
    const size_t base = lib.all().size();
    // install from a directory as a new voicebank
    copy_bank("yune", d / "src1"); edit_manifest(d / "src1", [](auto& j) { j["id"] = "mine"; j["name"] = "Mine"; j["version"] = "1.0.0"; });
    std::vector<Diagnostic> w;
    auto r = lib.install(d / "src1", false, &w); REQUIRE(r);
    CHECK(lib.all().size() == base + 1); CHECK((*r)->manifest.id == "mine"); CHECK_FALSE((*r)->bundled);
    CHECK(fs::exists(d / "user" / "mine-1.0.0" / "voicebank.json"));
    CHECK_FALSE(lib.install(d / "src1"));                                        // duplicate refused
    CHECK(lib.install(d / "src1", true));                                        // unless replacing
    // second version side by side
    copy_bank("yune", d / "src2"); edit_manifest(d / "src2", [](auto& j) { j["id"] = "mine"; j["version"] = "1.2.0"; });
    REQUIRE(lib.install(d / "src2"));
    CHECK(lib.versions_of("mine").size() == 2);
    CHECK(lib.find("mine")->manifest.version == "1.2.0");                        // newest by default
    CHECK(lib.find("mine", "1.0.0")->manifest.version == "1.0.0");               // exact when asked
    // bundled voicebanks cannot be removed
    CHECK_FALSE(lib.remove("yune", "1.0.0"));
    CHECK(fs::exists(fs::path(MELOVIO_BUNDLED_VOICEBANKS) / "yune" / "voicebank.json"));
    // removal
    REQUIRE(lib.remove("mine", "1.2.0")); CHECK(lib.find("mine")->manifest.version == "1.0.0");
    REQUIRE(lib.remove("mine", "1.0.0")); CHECK(lib.find("mine") == nullptr);
    CHECK_FALSE(lib.remove("mine", "1.0.0"));
    // an unusable voicebank is refused with its reasons
    copy_bank("yune", d / "src3"); edit_manifest(d / "src3", [](auto& j) { j["id"] = "bad"; j["synthesis"]["kind"] = "sample-based"; });
    auto bad = lib.install(d / "src3"); REQUIRE_FALSE(bad); CHECK(bad.error.find("sample-based") != std::string::npos);
    CHECK(lib.find("bad") == nullptr);
    CHECK_FALSE(lib.install(d / "nonexistent"));
}
TEST_CASE("voicebank packages (.mvb zip): export, import, hostile archives") {
    TempDir d; VoicebankLibrary lib; lib.set_user_dir(d / "user"); lib.refresh();
    const Voicebank* y = bundled_library().find("koharu"); REQUIRE(y);
    REQUIRE(lib.export_package(*y, d / "koharu.mvb"));
    REQUIRE(fs::file_size(d / "koharu.mvb") > 100);
    auto r = lib.install(d / "koharu.mvb"); REQUIRE(r);
    CHECK((*r)->manifest.id == "koharu"); CHECK((*r)->usable());
    CHECK((*r)->profile.vowel[0].f1 == doctest::Approx(862.f));
    // path traversal in an archive is rejected before anything is written
    mz_zip_archive z{}; const std::string evil = path_to_utf8(d / "evil.mvb");
    REQUIRE(mz_zip_writer_init_file(&z, evil.c_str(), 0));
    REQUIRE(mz_zip_writer_add_mem(&z, "../outside.txt", "x", 1, MZ_DEFAULT_LEVEL));
    REQUIRE(mz_zip_writer_finalize_archive(&z)); mz_zip_writer_end(&z);
    auto e = lib.install(d / "evil.mvb"); REQUIRE_FALSE(e); CHECK(e.error.find("unsafe") != std::string::npos);
    CHECK_FALSE(fs::exists(d / "user" / "outside.txt")); CHECK_FALSE(fs::exists(d / "outside.txt"));
    { std::ofstream f(d / "notzip.mvb"); f << "hello"; }
    CHECK_FALSE(lib.install(d / "notzip.mvb"));
    // staging directories never linger
    for (auto& e2 : fs::directory_iterator(d / "user")) CHECK(e2.path().filename().string().rfind(".staging", 0) != 0);
}
TEST_CASE("missing voicebank detection and relocation") {
    TempDir d; VoicebankLibrary lib; lib.set_user_dir(d / "user"); lib.refresh();     // empty library: nothing installed
    Project p = small_project("yune");
    auto res = lib.resolve(p, d.path);
    REQUIRE(res.size() == 1); CHECK(res[0].state == VoicebankResolution::State::Missing); CHECK_FALSE(res[0].ok());
    // user points at a folder holding the voicebank: stored as a RELATIVE hint, and the project resolves from it
    copy_bank("yune", d / "found" / "yune");
    REQUIRE(lib.locate(p, "yune", d / "found" / "yune", d.path));
    CHECK(p.voicebanks[0].path_hint == "found/yune");
    VoicebankLibrary fresh; fresh.set_user_dir(d / "user2"); fresh.refresh();
    auto res2 = fresh.resolve(p, d.path);
    REQUIRE(res2.size() == 1); CHECK(res2[0].state == VoicebankResolution::State::FoundAtHint); CHECK(res2[0].ok());
    // the wrong voicebank in the chosen folder is rejected
    copy_bank("koharu", d / "wrong");
    CHECK_FALSE(lib.locate(p, "yune", d / "wrong", d.path));
    // version mismatch is reported but usable
    VoicebankLibrary full = bundled_library(); p.voicebanks[0].version = "0.5.0"; p.voicebanks[0].path_hint.clear();
    auto res3 = full.resolve(p, d.path);
    CHECK(res3[0].state == VoicebankResolution::State::FoundOtherVersion); CHECK(res3[0].ok());
}
