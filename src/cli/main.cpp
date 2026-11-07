// melovio-cli: headless access to everything Melovio Studio does with data: validate, render, export, inspect voicebanks.
#include "core/config.hpp"
#include "core/export.hpp"
#include "core/midi.hpp"
#include "core/project_io.hpp"
#include "core/pronunciation.hpp"
#include "core/voicebank_library.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace melovio;

static void usage() {
    std::puts(
        "melovio-cli " MELOVIO_VERSION "\n"
        "usage:\n"
        "  melovio-cli voicebanks                         list installed voicebanks\n"
        "  melovio-cli validate-voicebank <dir> [--deep]  check a voicebank (--deep synthesises every kana)\n"
        "  melovio-cli install <dir|.mvb>                 import a voicebank into the user folder\n"
        "  melovio-cli package <dir> <out.mvb>            pack a voicebank directory into a .mvb file\n"
        "  melovio-cli validate <project.mvp>             preflight a project (voicebanks, lyrics, ranges)\n"
        "  melovio-cli render <project.mvp> <out.wav> [--sr N] [--bits 16|24|32] [--mono] [--normalize] [--track ID]\n"
        "                                                [--from-bar N --to-bar M]\n"
        "  melovio-cli export-midi <project.mvp> <out.mid>   write the project as a Standard MIDI File (lyrics included)\n"
        "  melovio-cli import-midi <in.mid> <out.mvp> [--voicebank ID] [--lyrics-from-sheet FILE.txt]   create a project from a MIDI file\n"
        "  melovio-cli analyze <lyric>                    show the phoneme sequence for a lyric\n"
        "  melovio-cli demo <out.mvp> [voicebank-id]      write a small demo project (Sakura)\n");
}

static fs::path exe_dir_of(const char* argv0) { std::error_code ec; auto p = fs::weakly_canonical(fs::path(argv0), ec); return p.has_parent_path() ? p.parent_path() : fs::current_path(); }

static void print_diags(const std::vector<Diagnostic>& d) {
    for (auto& x : d) std::printf("  [%s] %s\n", x.severity == Severity::Error ? "error" : x.severity == Severity::Warning ? "warn " : "info ", x.message.c_str());
}

static Project make_demo(const std::string& vb) {
    Project p; p.meta.title = "Sakura (demo)"; p.meta.comment = "Traditional melody, simplified. Made with melovio-cli demo.";
    p.tempo_map.set_tempi({ { 0, 72.0 } });
    Track& t = p.add_track("Lead", vb);
    struct N { int pitch; const char* ly; int beats4; };   // length in quarter-beats
    const N melody[] = { { 69, "さ", 4 }, { 69, "く", 4 }, { 71, "ら", 8 }, { 69, "さ", 4 }, { 69, "く", 4 }, { 71, "ら", 8 },
                         { 69, "さ", 4 }, { 71, "く", 4 }, { 72, "ら", 4 }, { 71, "さ", 4 }, { 69, "く", 4 }, { 71, "ら", 4 }, { 72, "ぁ", 4 }, { 71, "ー", 4 } };
    Tick tk = 0; const int q = p.tempo_map.ppq() / 4;
    for (auto& n : melody) { Note nn; nn.tick = tk; nn.length = n.beats4 * q - 20; nn.pitch = n.pitch; nn.lyric = n.ly; p.add_note(t, nn); tk += n.beats4 * q; }
    p.ensure_voicebank_ref(vb, "1.0.0", vb);
    return p;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string cmd = argv[1];
    VoicebankLibrary lib; setup_default_library(lib, exe_dir_of(argv[0]));

    if (cmd == "voicebanks") {
        for (auto& v : lib.all()) std::printf("%-10s %-8s %-9s %s  [%s]%s\n", v.manifest.id.c_str(), v.manifest.version.c_str(), v.bundled ? "bundled" : "installed",
            v.manifest.name.c_str(), v.usable() ? "ok" : "UNUSABLE", v.manifest.native_name.empty() ? "" : ("  " + v.manifest.native_name).c_str());
        if (lib.all().empty()) std::puts("(no voicebanks found)");
        return 0;
    }
    if (cmd == "validate-voicebank" && argc >= 3) {
        auto r = load_voicebank(path_from_utf8(argv[2]));
        if (!r) { std::printf("error: %s\n", r.error.c_str()); return 1; }
        std::printf("%s %s: %s\n", r->manifest.id.c_str(), r->manifest.version.c_str(), r->usable() ? "usable" : "NOT usable");
        print_diags(r->diagnostics);
        int rc = r->usable() ? 0 : 1;
        if (argc >= 4 && !std::strcmp(argv[3], "--deep") && r->usable()) {
            auto rep = check_completeness(*r);
            std::printf("completeness: %zu/%zu units synthesised cleanly\n", rep.passed, rep.total);
            for (auto& c : rep.checks) if (!c.ok) std::printf("  FAIL %s (%s): %s\n", c.unit.c_str(), c.phonemes.c_str(), c.problem.c_str());
            print_diags(rep.diagnostics); if (!rep.complete()) rc = 1;
        }
        return rc;
    }
    if (cmd == "install" && argc >= 3) {
        std::vector<Diagnostic> w; auto r = lib.install(path_from_utf8(argv[2]), false, &w);
        if (!r) { std::printf("error: %s\n", r.error.c_str()); return 1; }
        std::printf("installed %s %s into %s\n", (*r)->manifest.id.c_str(), (*r)->manifest.version.c_str(), path_to_utf8((*r)->root).c_str()); print_diags(w); return 0;
    }
    if (cmd == "package" && argc >= 4) {
        auto r = load_voicebank(path_from_utf8(argv[2]));
        if (!r) { std::printf("error: %s\n", r.error.c_str()); return 1; }
        Status s = lib.export_package(*r, path_from_utf8(argv[3]));
        std::printf("%s\n", s ? "ok" : s.message.c_str()); return s ? 0 : 1;
    }
    if (cmd == "export-midi" && argc >= 4) {
        auto pr = load_project(path_from_utf8(argv[2])); if (!pr) { std::printf("error: %s\n", pr.error.c_str()); return 1; }
        Status st = write_midi_file(path_from_utf8(argv[3]), *pr);
        std::printf("%s\n", st ? "ok" : st.message.c_str()); return st ? 0 : 1;
    }
    if (cmd == "import-midi" && argc >= 4) {
        std::string vb = "yune", sheet;
        for (int i = 4; i + 1 < argc; ++i) { if (!std::strcmp(argv[i], "--voicebank")) vb = argv[++i]; else if (!std::strcmp(argv[i], "--lyrics-from-sheet")) sheet = argv[++i]; }
        auto mf = read_midi_file(path_from_utf8(argv[2])); if (!mf) { std::printf("error: %s\n", mf.error.c_str()); return 1; }
        Project p; MidiImportOptions o; o.voicebank_id = vb; o.import_tempo = true;
        for (size_t i = 0; i < mf->tracks.size(); ++i) o.tracks.push_back(i);
        std::vector<Diagnostic> d; auto r = import_midi(p, *mf, o, &d);
        if (!r) { std::printf("error: %s\n", r.error.c_str()); return 1; }
        if (!sheet.empty()) {
            std::string err; const std::string txt = read_text_file(path_from_utf8(sheet), &err);
            if (!err.empty()) { std::printf("error: %s\n", err.c_str()); return 1; }
            auto units = parse_lyric_sheet(txt, LyricSheetMode::Morae); size_t k = 0;
            for (auto& t : p.tracks) for (auto& n : t.notes) if (k < units.size()) n.lyric = units[k++];
            std::printf("assigned %zu of %zu sheet units\n", k, units.size());
        }
        print_diags(d);
        Status st = save_project(p, path_from_utf8(argv[3]));
        std::printf("%zu track(s), %zu note(s): %s\n", r->tracks_added, r->notes_added, st ? "ok" : st.message.c_str()); return st ? 0 : 1;
    }
    if (cmd == "analyze" && argc >= 3) {
        PronunciationEngine pe; auto a = pe.analyze(argv[2]);
        std::printf("phonemes: %s\nsyllables:", a.phoneme_text.c_str());
        for (auto& s : a.syllables) std::printf(" [%s => %s]", s.source.c_str(), s.token.c_str());
        std::puts(""); print_diags(a.diagnostics); return a.pronounceable ? 0 : 1;
    }
    if (cmd == "demo" && argc >= 3) {
        Project p = make_demo(argc >= 4 ? argv[3] : "yune");
        Status s = save_project(p, path_from_utf8(argv[2]));
        std::printf("%s\n", s ? "ok" : s.message.c_str()); return s ? 0 : 1;
    }
    if ((cmd == "validate" || cmd == "render") && argc >= 3) {
        const fs::path pf = path_from_utf8(argv[2]);
        std::vector<Diagnostic> warn; auto pr = load_project(pf, &warn);
        if (!pr) { std::printf("error: %s\n", pr.error.c_str()); return 1; }
        Project& p = *pr;
        auto res = lib.resolve(p, pf.has_parent_path() ? pf.parent_path() : fs::current_path());
        for (auto& r : res) if (!r.ok()) { std::printf("error: voicebank '%s': %s\n", r.id.c_str(), r.message.c_str()); }
        auto banks = snapshot_for_project(res);
        PronunciationEngine pe;
        auto pre = preflight(p, banks, pe);
        if (cmd == "validate") {
            std::printf("%zu track(s), %.1f s, %zu diagnostic(s)\n", p.tracks.size(), p.duration_sec(), pre.size());
            print_diags(pre); return has_errors(pre) ? 1 : 0;
        }
        if (argc < 4) { usage(); return 2; }
        ExportSettings es; int from_bar = -1, to_bar = -1;
        for (int i = 4; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--sr" && i + 1 < argc) es.sample_rate = std::atoi(argv[++i]);
            else if (a == "--bits" && i + 1 < argc) es.bit_depth = std::atoi(argv[++i]);
            else if (a == "--mono") es.channels = 1;
            else if (a == "--normalize") es.normalize = true;
            else if (a == "--track" && i + 1 < argc) { es.scope = ExportScope::Track; es.track_id = argv[++i]; }
            else if (a == "--from-bar" && i + 1 < argc) from_bar = std::atoi(argv[++i]);
            else if (a == "--to-bar" && i + 1 < argc) to_bar = std::atoi(argv[++i]);
            else { std::printf("unknown option %s\n", a.c_str()); return 2; }
        }
        if (from_bar >= 0 && to_bar > from_bar) { es.scope = ExportScope::Region; es.region_start = p.tempo_map.bar_start_tick(from_bar - 1); es.region_end = p.tempo_map.bar_start_tick(to_bar - 1); }
        Renderer rend(banks);
        int last = -1;
        auto r = export_wav(p, rend, es, path_from_utf8(argv[3]), [&](double f, const std::string&) { int pc = static_cast<int>(f * 100); if (pc != last && pc % 25 == 0) { std::printf("  %d%%\n", pc); last = pc; } });
        if (!r) { std::printf("render failed: %s\n", r.error.c_str()); return 1; }
        std::printf("wrote %s  (%.2f s, peak %.3f%s)\n", argv[3], r->duration_sec, r->peak, r->clipped ? ", CLIPPED" : "");
        print_diags(r->diagnostics); return 0;
    }
    usage(); return 2;
}
