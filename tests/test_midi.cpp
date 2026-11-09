#include "test_util.hpp"
#include "core/midi.hpp"

using namespace melovio;

namespace {
    Project sample_project() {
        Project p;
        p.tempo_map.set_tempi({ { 0, 172.0 }, { 480 * 8, 120.0 } });
        p.tempo_map.set_signatures({ { 0, 4, 4 }, { 2, 3, 4 } });
        p.add_track("Kyo", "yune");
        p.add_track("Yune", "yune");
        Track& a = p.tracks[0];
        Track& b = p.tracks[1];
        const char* ly[] = { "さ", "く", "ら" };
        for (int i = 0; i < 3; ++i) { Note n; n.tick = i * 480; n.length = 400; n.pitch = 69 + i; n.lyric = ly[i]; n.velocity = 0.8f; p.add_note(a, n); }
        Note m; m.tick = 240; m.length = 960; m.pitch = 60; m.lyric = "ねえ"; m.velocity = 0.5f; p.add_note(b, m);
        return p;
    }

    std::string be16(int v) { return std::string{ static_cast<char>(v >> 8), static_cast<char>(v & 255) }; }
    std::string chunk(const char* id, const std::string& body) { std::string s = id; for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((body.size() >> (8 * i)) & 255)); return s + body; }
    std::string header(int format, int n, int div) { return chunk("MThd", be16(format) + be16(n) + be16(div)); }
}

TEST_CASE("midi: project -> MIDI -> project round trip keeps notes, lyrics, tempo and signatures") {
    Project p = sample_project();
    auto bytes = encode_midi(p);
    REQUIRE(bytes.ok());
    auto f = parse_midi(*bytes);
    REQUIRE(f.ok());
    CHECK(f->ppq == 480);
    REQUIRE(f->tracks.size() == 2);
    CHECK(f->tracks[0].name == "Kyo");
    CHECK(f->tracks[0].lyric_count == 3);
    REQUIRE(f->tempi.size() == 2);
    CHECK(f->tempi[0].bpm == doctest::Approx(172.0).epsilon(1e-4));
    CHECK(f->tempi[1].tick == 480 * 8);
    REQUIRE(f->signatures.size() == 2);
    CHECK(f->signatures[1].numerator == 3);

    Project q;
    MidiImportOptions o; o.tracks = { 0, 1 }; o.import_tempo = true; o.voicebank_id = "yune";
    auto s = import_midi(q, *f, o);
    REQUIRE(s.ok());
    REQUIRE(q.tracks.size() == 2);
    REQUIRE(q.tracks[0].notes.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(q.tracks[0].notes[i].tick == p.tracks[0].notes[i].tick);
        CHECK(q.tracks[0].notes[i].length == p.tracks[0].notes[i].length);
        CHECK(q.tracks[0].notes[i].pitch == p.tracks[0].notes[i].pitch);
        CHECK(q.tracks[0].notes[i].lyric == p.tracks[0].notes[i].lyric);
        CHECK(q.tracks[0].notes[i].velocity == doctest::Approx(0.8f).epsilon(0.01));
    }
    CHECK(q.tracks[1].notes[0].lyric == "ねえ");
    CHECK(q.tempo_map.tempi().size() == 2);
    CHECK(q.tempo_map.signatures()[1].bar == 2);
    CHECK(q.tempo_map.signatures()[1].numerator == 3);
}

TEST_CASE("midi: running status, note-on velocity 0 as note-off, and ppq conversion") {
    std::string t;
    t += std::string("\x00\xFF\x03\x04Lead", 8);
    t += std::string("\x00\x90\x3C\x64", 4);          // note on C4 vel 100
    t += std::string("\x30\x3C\x00", 3);              // running status: same note, vel 0 -> off after 48 ticks (ppq 96)
    t += std::string("\x00\x40\x50", 3);              // running status: E4 on
    t += std::string("\x60\x40\x00", 3);              // off after 96 ticks
    t += std::string("\x00\xFF\x2F\x00", 4);
    auto f = parse_midi(header(1, 1, 96) + chunk("MTrk", t));
    REQUIRE(f.ok());
    REQUIRE(f->tracks.size() == 1);
    REQUIRE(f->tracks[0].notes.size() == 2);
    CHECK(f->tracks[0].notes[0].length == 48);
    CHECK(f->tracks[0].notes[1].tick == 48);
    Project p; MidiImportOptions o; o.tracks = { 0 };
    REQUIRE(import_midi(p, *f, o).ok());
    CHECK(p.tracks[0].notes[0].length == 240);        // 48 ticks @96 -> 240 @480
    CHECK(p.tracks[0].notes[1].tick == 240);
}

TEST_CASE("midi: format 0 is split by channel") {
    std::string t;
    t += std::string("\x00\x90\x3C\x64\x30\x80\x3C\x00", 8);
    t += std::string("\x00\x91\x40\x64\x30\x81\x40\x00", 8);
    t += std::string("\x00\xFF\x2F\x00", 4);
    auto f = parse_midi(header(0, 1, 480) + chunk("MTrk", t));
    REQUIRE(f.ok());
    CHECK(f->tracks.size() == 2);
}

TEST_CASE("midi: chords keep the highest note and are reported") {
    std::string t;
    t += std::string("\x00\x90\x3C\x64\x00\x90\x40\x64\x00\x90\x43\x64\x60\x80\x3C\x00\x00\x80\x40\x00\x00\x80\x43\x00", 24);
    t += std::string("\x00\xFF\x2F\x00", 4);
    auto f = parse_midi(header(1, 1, 480) + chunk("MTrk", t)); REQUIRE(f.ok());
    Project p; MidiImportOptions o; o.tracks = { 0 }; std::vector<Diagnostic> d;
    auto s = import_midi(p, *f, o, &d); REQUIRE(s.ok());
    REQUIRE(p.tracks[0].notes.size() == 1);
    CHECK(p.tracks[0].notes[0].pitch == 67);
    CHECK(s->notes_lost_to_overlap == 2);
    CHECK_FALSE(d.empty());
}

TEST_CASE("midi: lyrics from Text events are used only when they sit on notes; bad encodings are skipped") {
    std::string t;
    t += std::string("\x00\xFF\x01\x02" "\xE3\x81", 6);          // truncated UTF-8 sequence is not valid
    t += std::string("\x00\x90\x3C\x64\x60\x80\x3C\x00", 8);
    t += std::string("\x00\xFF\x01\x03" "\xE3\x81\x82", 7);
    t += std::string("\x00\x90\x3E\x64\x60\x80\x3E\x00", 8);
    t += std::string("\x00\xFF\x2F\x00", 4);
    auto f = parse_midi(header(1, 1, 480) + chunk("MTrk", t)); REQUIRE(f.ok());
    REQUIRE(f->tracks.size() == 1);
    CHECK(f->tracks[0].lyric_count <= 1);
}

TEST_CASE("midi: hard errors are reported, not crashed on") {
    CHECK_FALSE(parse_midi("hello").ok());
    CHECK_FALSE(parse_midi(header(1, 1, 0x8000 | 0x2800)).ok());               // SMPTE
    CHECK_FALSE(parse_midi(header(1, 1, 480)).ok());                           // no tracks
    std::string t = std::string("\x00\x90\x3C\x64", 4);                        // truncated track, no note-off
    auto f = parse_midi(header(1, 1, 480) + chunk("MTrk", t) .substr(0, 10));
    CHECK(f.ok() == false);
    auto g = parse_midi(header(1, 1, 480) + chunk("MTrk", t));                 // missing end-of-track: note is closed with a warning
    REQUIRE(g.ok());
    CHECK_FALSE(g->diagnostics.empty());
    std::string junk; for (int i = 0; i < 300; ++i) junk.push_back(static_cast<char>(i * 37));
    parse_midi(header(1, 1, 480) + chunk("MTrk", junk));                       // must not crash
}

TEST_CASE("midi: write_midi_file writes a readable file and can export one track") {
    tu::TempDir d; Project p = sample_project();
    MidiExportOptions o; o.track_ids = { p.tracks[1].id };
    REQUIRE(write_midi_file(d / "x.mid", p, o));
    auto f = read_midi_file(d / "x.mid"); REQUIRE(f.ok());
    REQUIRE(f->tracks.size() == 1);
    CHECK(f->tracks[0].name == "Yune");
    MidiExportOptions none; none.track_ids = { "nope" };
    CHECK_FALSE(encode_midi(p, none).ok());
}

TEST_CASE("lyric sheet: headers and punctuation are dropped, words and morae modes") {
    const std::string sheet = "[Intro — Kyo]\n\nはーい！　みんな！\nきょうも　げんき？\n[Chorus]\nシュガー！\n";
    auto w = parse_lyric_sheet(sheet, LyricSheetMode::Words);
    REQUIRE(w.size() == 5);
    CHECK(w[0] == "はーい");
    CHECK(w[2] == "きょうも");
    CHECK(w[4] == "シュガー");
    auto m = parse_lyric_sheet("きょうも　げんき？", LyricSheetMode::Morae);
    CHECK(m.size() == 6);                              // きょ う も げ ん き
    CHECK(m[0] == "きょ");
    auto keep = parse_lyric_sheet("[x]\nあ", LyricSheetMode::Words, false);
    CHECK(keep.size() == 2);
    auto group = parse_lyric_sheet("{k a} あ", LyricSheetMode::Words);
    CHECK(group.size() == 2);
    CHECK(group[0] == "{k a}");
}

TEST_CASE("markers: sorted insert, section_end, JSON round trip, MIDI round trip") {
    Project p = sample_project();
    p.add_marker(480 * 8, "Chorus");
    p.add_marker(0, "Intro");
    p.add_marker(480 * 4, "Verse");
    REQUIRE(p.markers.size() == 3);
    CHECK(p.markers[0].name == "Intro");
    CHECK(p.markers[2].name == "Chorus");
    CHECK(p.section_end(p.markers[0]) == 480 * 4);
    CHECK(p.section_end(p.markers[2]) > p.markers[2].tick);       // last section: runs to the end of the song
    CHECK(p.find_marker(p.markers[1].id) != nullptr);

    auto q = project_from_json(project_to_json(p));
    REQUIRE(q.ok());
    REQUIRE(q->markers.size() == 3);
    CHECK(q->markers[1].name == "Verse");
    CHECK(q->markers[1].tick == 480 * 4);
    CHECK(q->markers[1].id == p.markers[1].id);

    // Older files have no "markers" key and still load.
    Project plain = sample_project();
    auto txt = project_to_json(plain);
    auto r = project_from_json(txt);
    REQUIRE(r.ok());
    CHECK(r->markers.empty());

    auto bytes = encode_midi(p);
    REQUIRE(bytes.ok());
    auto f = parse_midi(*bytes);
    REQUIRE(f.ok());
    REQUIRE(f->markers.size() == 3);
    CHECK(f->markers[2].name == "Chorus");
    Project dst;
    MidiImportOptions o; o.tracks = { 0 }; o.voicebank_id = "yune";
    REQUIRE(import_midi(dst, *f, o).ok());
    REQUIRE(dst.markers.size() == 3);
    CHECK(dst.markers[1].tick == 480 * 4);
    MidiImportOptions o2 = o; o2.import_markers = false;
    Project dst2; REQUIRE(import_midi(dst2, *f, o2).ok());
    CHECK(dst2.markers.empty());

    p.remove_marker(p.markers[1].id);
    CHECK(p.markers.size() == 2);
}
