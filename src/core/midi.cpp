#include "midi.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace melovio {
    namespace {
        struct Reader {
            const std::string& d; size_t pos;
            size_t left() const { return d.size() - pos; }
            bool u8(int& v) { if (left() < 1) return false; v = static_cast<unsigned char>(d[pos++]); return true; }
            bool u16(int& v) { int a, b; if (!u8(a) || !u8(b)) return false; v = (a << 8) | b; return true; }
            bool u32(uint32_t& v) { int a, b, c, e; if (!u8(a) || !u8(b) || !u8(c) || !u8(e)) return false; v = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(e); return true; }
            bool vlq(uint32_t& v) { v = 0; for (int i = 0; i < 4; ++i) { int b; if (!u8(b)) return false; v = (v << 7) | static_cast<uint32_t>(b & 0x7f); if (!(b & 0x80)) return true; } return false; }
        };

        bool valid_utf8(const std::string& s) {
            size_t i = 0;
            while (i < s.size()) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const size_t n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 99;
                if (n == 99) return false;
                for (size_t k = 1; k <= n; ++k) if (i + k >= s.size() || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
                i += n + 1;
            }
            return true;
        }

        std::string trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' || s[a] == '\0')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == '\0')) --b;
            return s.substr(a, b - a);
        }

        struct RawText { Tick tick; std::string text; };
        struct RawTrack {
            std::string name;
            std::vector<MidiNote> notes;
            std::vector<RawText> lyrics, texts;
        };

        void add_diag(std::vector<Diagnostic>& d, Severity s, const char* code, std::string msg) { d.push_back({ s, code, std::move(msg) }); }

        void attach_lyrics(RawTrack& t, int ppq, std::vector<Diagnostic>& diags, size_t& lyric_count, size_t index) {
            std::vector<RawText>* src = &t.lyrics;
            if (t.lyrics.empty() && !t.texts.empty()) {
                // Some tools store lyrics in generic Text events. Only trust them when most of them sit on note starts.
                size_t hit = 0;
                for (auto& x : t.texts) for (auto& n : t.notes) if (n.tick == x.tick) { ++hit; break; }
                if (hit * 2 >= t.texts.size() && hit > 0) src = &t.texts;
            }
            std::stable_sort(t.notes.begin(), t.notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.tick < b.tick; });
            size_t bad_enc = 0, ignored = 0, orphan = 0;
            for (auto& x : *src) {
                std::string s = trim(x.text);
                while (!s.empty() && (s[0] == '/' || s[0] == '\\' || s[0] == '%')) s = trim(s.substr(1));
                if (s.empty()) continue;
                if (!valid_utf8(s)) { ++bad_enc; continue; }
                if (s == "-" || s == "+") { ++ignored; continue; }
                MidiNote* target = nullptr;
                for (auto& n : t.notes) if (n.tick == x.tick && n.lyric.empty()) { target = &n; break; }
                if (!target) for (auto& n : t.notes) if (n.tick >= x.tick && n.tick - x.tick <= ppq / 4 && n.lyric.empty()) { target = &n; break; }
                if (!target) { ++orphan; continue; }
                target->lyric = s; ++lyric_count;
            }
            const std::string who = "MIDI track " + std::to_string(index + 1) + " (" + (t.name.empty() ? "unnamed" : t.name) + ")";
            if (bad_enc) add_diag(diags, Severity::Warning, "midi.lyric_encoding", who + ": " + std::to_string(bad_enc) + " lyric event(s) are not UTF-8 (probably Shift-JIS) and were skipped.");
            if (ignored) add_diag(diags, Severity::Info, "midi.lyric_continuation", who + ": " + std::to_string(ignored) + " continuation marker(s) (- or +) ignored.");
            if (orphan) add_diag(diags, Severity::Warning, "midi.lyric_orphan", who + ": " + std::to_string(orphan) + " lyric event(s) did not line up with a note start and were skipped.");
        }
    }

    Result<MidiFile> parse_midi(const std::string& bytes) {
        using R = Result<MidiFile>;
        size_t start = bytes.find("MThd");
        if (start == std::string::npos || start > 64) return R::fail("not a MIDI file (no MThd header)");
        Reader r{ bytes, start + 4 };
        uint32_t hlen = 0; int format = 0, ntrks = 0, division = 0;
        if (!r.u32(hlen) || hlen < 6 || !r.u16(format) || !r.u16(ntrks) || !r.u16(division)) return R::fail("truncated MIDI header");
        r.pos += hlen - 6;
        if (format > 2) return R::fail("unsupported MIDI format " + std::to_string(format));
        if (division & 0x8000) return R::fail("SMPTE time division is not supported (the file must use ticks per quarter note)");
        if (division == 0) return R::fail("invalid MIDI time division 0");
        MidiFile f; f.format = format; f.ppq = division;

        std::vector<RawTrack> raw;
        for (int ti = 0; ti < ntrks; ++ti) {
            uint32_t len = 0;
            // skip unknown chunks
            for (;;) {
                if (r.left() < 8) break;
                std::string id = bytes.substr(r.pos, 4); r.pos += 4; r.u32(len);
                if (id == "MTrk") break;
                r.pos += std::min<size_t>(len, r.left());
            }
            if (r.left() < 1 && len == 0) { add_diag(f.diagnostics, Severity::Warning, "midi.missing_track", "the header announced " + std::to_string(ntrks) + " tracks but only " + std::to_string(ti) + " were found"); break; }
            size_t end = r.pos + len;
            if (end > bytes.size()) { add_diag(f.diagnostics, Severity::Warning, "midi.truncated", "track " + std::to_string(ti + 1) + " is truncated; reading what is there"); end = bytes.size(); }
            RawTrack t; Tick tick = 0; int running = 0;
            struct Open { Tick tick; int vel; };
            std::map<std::pair<int, int>, std::vector<Open>> open;
            size_t unterminated = 0;
            bool ended = false;
            while (r.pos < end && !ended) {
                uint32_t dt; if (!r.vlq(dt)) break;
                tick += dt;
                int status; if (!r.u8(status)) break;
                if (status < 0x80) { if (!running) { add_diag(f.diagnostics, Severity::Warning, "midi.bad_status", "track " + std::to_string(ti + 1) + ": data byte without a status; stopping this track"); break; } --r.pos; status = running; }
                if (status == 0xFF) {
                    int type; uint32_t ml; if (!r.u8(type) || !r.vlq(ml) || ml > r.left()) break;
                    const std::string data = bytes.substr(r.pos, ml); r.pos += ml;
                    if (type == 0x2F) ended = true;
                    else if (type == 0x03 && t.name.empty()) t.name = trim(data);
                    else if (type == 0x05) t.lyrics.push_back({ tick, data });
                    else if (type == 0x06) { const std::string nm = trim(data); if (!nm.empty() && valid_utf8(nm)) f.markers.push_back({ tick, nm }); }
                    else if (type == 0x01) t.texts.push_back({ tick, data });
                    else if (type == 0x51 && ml == 3) { const double us = (static_cast<unsigned char>(data[0]) << 16) | (static_cast<unsigned char>(data[1]) << 8) | static_cast<unsigned char>(data[2]); if (us > 0) f.tempi.push_back({ tick, std::round(60000000.0 / us * 1000.0) / 1000.0 }); }
                    else if (type == 0x58 && ml >= 2) { const int dd = static_cast<unsigned char>(data[1]); if (dd <= 5) f.signatures.push_back({ tick, std::max(1, static_cast<int>(static_cast<unsigned char>(data[0]))), 1 << dd }); }
                    continue;
                }
                if (status == 0xF0 || status == 0xF7) { uint32_t sl; if (!r.vlq(sl) || sl > r.left()) break; r.pos += sl; running = 0; continue; }
                if (status >= 0xF0) { running = 0; continue; }
                running = status;
                const int kind = status & 0xF0, ch = status & 0x0F;
                const int nd = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                int a = 0, b = 0;
                if (!r.u8(a)) break;
                if (nd == 2 && !r.u8(b)) break;
                if (kind == 0x90 && b > 0) open[{ ch, a }].push_back({ tick, b });
                else if (kind == 0x80 || kind == 0x90) {
                    auto it = open.find({ ch, a });
                    if (it != open.end() && !it->second.empty()) {
                        Open o = it->second.front(); it->second.erase(it->second.begin());
                        MidiNote n; n.tick = o.tick; n.length = std::max<Tick>(1, tick - o.tick); n.pitch = a; n.channel = ch; n.velocity = o.vel; t.notes.push_back(n);
                    }
                }
            }
            for (auto& [k, v] : open) for (auto& o : v) { MidiNote n; n.tick = o.tick; n.length = std::max<Tick>(1, tick - o.tick); n.pitch = k.second; n.channel = k.first; n.velocity = o.vel; t.notes.push_back(n); ++unterminated; }
            if (unterminated) add_diag(f.diagnostics, Severity::Warning, "midi.unterminated", "track " + std::to_string(ti + 1) + ": " + std::to_string(unterminated) + " note(s) had no note-off and were ended at the end of the track");
            r.pos = end;
            raw.push_back(std::move(t));
        }
        if (raw.empty()) return R::fail("the MIDI file contains no tracks");

        auto sort_by_tick = [](auto& v) { std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; }); };
        sort_by_tick(f.tempi); sort_by_tick(f.signatures); sort_by_tick(f.markers);

        size_t idx = 0;
        for (auto& t : raw) {
            if (t.notes.empty()) { ++idx; continue; }
            if (format == 0) {
                std::map<int, RawTrack> by_ch;
                for (auto& n : t.notes) { auto& c = by_ch[n.channel]; c.notes.push_back(n); c.name = "Channel " + std::to_string(n.channel + 1); c.lyrics = t.lyrics; c.texts = t.texts; }
                for (auto& [ch, c] : by_ch) {
                    MidiTrack mt; mt.name = c.name; attach_lyrics(c, f.ppq, f.diagnostics, mt.lyric_count, idx); mt.notes = std::move(c.notes); f.tracks.push_back(std::move(mt));
                }
            } else {
                MidiTrack mt; mt.name = t.name; attach_lyrics(t, f.ppq, f.diagnostics, mt.lyric_count, idx); mt.notes = std::move(t.notes);
                if (mt.name.empty()) mt.name = "Track " + std::to_string(idx + 1);
                f.tracks.push_back(std::move(mt));
            }
            ++idx;
        }
        if (f.tracks.empty()) return R::fail("the MIDI file contains no notes");
        return R::success(std::move(f));
    }

    Result<MidiFile> read_midi_file(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return Result<MidiFile>::fail("cannot open " + path_to_utf8(path));
        std::ostringstream ss; ss << in.rdbuf();
        return parse_midi(ss.str());
    }

    Result<MidiImportSummary> import_midi(Project& project, const MidiFile& file, const MidiImportOptions& opt, std::vector<Diagnostic>* diags) {
        using R = Result<MidiImportSummary>;
        if (opt.tracks.empty()) return R::fail("no MIDI tracks were chosen");
        for (size_t i : opt.tracks) if (i >= file.tracks.size()) return R::fail("MIDI track index out of range");
        const int P = project.tempo_map.ppq();
        auto conv = [&](Tick t) { return static_cast<Tick>(std::llround(static_cast<double>(t) * P / file.ppq)); };

        if (opt.import_tempo) {
            std::vector<TempoChange> tc;
            for (auto& t : file.tempi) { Tick k = conv(t.tick); if (!tc.empty() && tc.back().tick == k) tc.back().bpm = t.bpm; else tc.push_back({ k, t.bpm }); }
            if (tc.empty() || tc.front().tick != 0) tc.insert(tc.begin(), { 0, tc.empty() ? 120.0 : tc.front().bpm });
            project.tempo_map.set_tempi(tc);
            std::vector<TimeSigChange> sc; Tick prev_tick = 0; int prev_bar = 0, pn = 4, pd = 4;
            for (auto& s : file.signatures) {
                const Tick tpb = static_cast<Tick>(file.ppq) * 4 * pn / pd;
                const Tick span = s.tick - prev_tick;
                if (tpb > 0 && span % tpb != 0 && diags) diags->push_back({ Severity::Warning, "midi.signature_misaligned", "a time-signature change does not start on a bar line; it was moved to the nearest bar" });
                const int bar = prev_bar + static_cast<int>(tpb > 0 ? (span + tpb / 2) / tpb : 0);
                if (!sc.empty() && sc.back().bar == bar) sc.back() = { bar, s.numerator, s.denominator }; else sc.push_back({ bar, s.numerator, s.denominator });
                prev_tick = s.tick; prev_bar = bar; pn = s.numerator; pd = s.denominator;
            }
            if (sc.empty() || sc.front().bar != 0) sc.insert(sc.begin(), { 0, 4, 4 });
            project.tempo_map.set_signatures(sc);
        }

        if (opt.import_markers) for (auto& m : file.markers) project.add_marker(conv(m.tick), m.name);
        MidiImportSummary sum;
        for (size_t i : opt.tracks) {
            const MidiTrack& mt = file.tracks[i];
            Track& t = project.add_track(mt.name.empty() ? "Track " + std::to_string(i + 1) : mt.name, opt.voicebank_id);
            std::vector<MidiNote> notes = mt.notes;
            std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.tick != b.tick ? a.tick < b.tick : a.pitch < b.pitch; });
            size_t added = 0;
            for (auto& n : notes) {
                Note x; x.tick = conv(n.tick); x.length = std::max<Tick>(1, conv(n.tick + n.length) - x.tick); x.pitch = std::clamp(n.pitch, 0, 127);
                x.velocity = std::clamp(static_cast<float>(n.velocity) / 127.f, 0.05f, 1.f);
                if (n.lyric.empty()) { x.lyric = opt.default_lyric; ++sum.notes_without_lyric; } else x.lyric = n.lyric;
                project.add_note(t, x); ++added;
            }
            sum.notes_added += t.notes.size(); sum.notes_lost_to_overlap += added - t.notes.size(); ++sum.tracks_added;
        }
        if (sum.notes_lost_to_overlap && diags) diags->push_back({ Severity::Warning, "midi.overlap", std::to_string(sum.notes_lost_to_overlap) + " note(s) were dropped because voices are monophonic (chords keep their highest note; overlapping notes are trimmed)." });
        if (sum.notes_without_lyric && diags) diags->push_back({ Severity::Info, "midi.no_lyric", std::to_string(sum.notes_without_lyric) + " note(s) had no lyric and got \"" + opt.default_lyric + "\"." });
        return R::success(sum);
    }

    namespace {
        void put_vlq(std::string& s, uint32_t v) {
            unsigned char buf[5]; int n = 0; buf[n++] = v & 0x7f; v >>= 7;
            while (v) { buf[n++] = static_cast<unsigned char>((v & 0x7f) | 0x80); v >>= 7; }
            while (n) s.push_back(static_cast<char>(buf[--n]));
        }
        void put_u16(std::string& s, int v) { s.push_back(static_cast<char>((v >> 8) & 0xff)); s.push_back(static_cast<char>(v & 0xff)); }
        void put_u32(std::string& s, uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); }

        struct Ev { Tick tick; int order; std::string bytes; };
        std::string track_chunk(std::vector<Ev> evs, const std::string& name) {
            std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
            std::string body;
            put_vlq(body, 0); body += "\xFF\x03"; put_vlq(body, static_cast<uint32_t>(name.size())); body += name;
            Tick last = 0;
            for (auto& e : evs) { put_vlq(body, static_cast<uint32_t>(e.tick - last)); body += e.bytes; last = e.tick; }
            put_vlq(body, 0); body += "\xFF\x2F"; body.push_back('\0');
            std::string out = "MTrk"; put_u32(out, static_cast<uint32_t>(body.size())); out += body; return out;
        }
    }

    Result<std::string> encode_midi(const Project& p, const MidiExportOptions& opt) {
        using R = Result<std::string>;
        std::vector<const Track*> tracks;
        for (auto& t : p.tracks) {
            if (!opt.track_ids.empty() && std::find(opt.track_ids.begin(), opt.track_ids.end(), t.id) == opt.track_ids.end()) continue;
            if (!opt.include_muted && t.mute) continue;
            tracks.push_back(&t);
        }
        if (tracks.empty()) return R::fail("no tracks to export");
        const int ppq = p.tempo_map.ppq();
        if (ppq < 1 || ppq > 0x7fff) return R::fail("the project resolution cannot be written to a MIDI file");

        std::vector<Ev> cond;
        for (auto& t : p.tempo_map.tempi()) {
            const uint32_t us = static_cast<uint32_t>(std::llround(60000000.0 / std::clamp(t.bpm, 1.0, 1000.0)));
            std::string b = "\xFF\x51\x03"; b.push_back(static_cast<char>((us >> 16) & 0xff)); b.push_back(static_cast<char>((us >> 8) & 0xff)); b.push_back(static_cast<char>(us & 0xff));
            cond.push_back({ t.tick, 1, b });
        }
        for (auto& s : p.tempo_map.signatures()) {
            int dd = 0; while ((1 << dd) < s.denominator && dd < 5) ++dd;
            std::string b = "\xFF\x58\x04"; b.push_back(static_cast<char>(s.numerator)); b.push_back(static_cast<char>(dd)); b.push_back(24); b.push_back(8);
            cond.push_back({ p.tempo_map.bar_start_tick(s.bar), 0, b });
        }
        for (auto& m : p.markers) { std::string b = "\xFF\x06"; put_vlq(b, static_cast<uint32_t>(m.name.size())); b += m.name; cond.push_back({ m.tick, 3, b }); }
        std::string out = "MThd"; put_u32(out, 6); put_u16(out, 1); put_u16(out, static_cast<int>(tracks.size()) + 1); put_u16(out, ppq);
        out += track_chunk(std::move(cond), p.meta.title.empty() ? "Melovio" : p.meta.title);
        for (const Track* t : tracks) {
            std::vector<Ev> ev;
            for (auto& n : t->notes) {
                const int vel = std::clamp(static_cast<int>(std::lround(n.velocity * 127.f)), 1, 127);
                if (!n.lyric.empty()) { std::string b = "\xFF\x05"; put_vlq(b, static_cast<uint32_t>(n.lyric.size())); b += n.lyric; ev.push_back({ n.tick, 1, b }); }
                std::string on; on.push_back(static_cast<char>(0x90)); on.push_back(static_cast<char>(n.pitch & 0x7f)); on.push_back(static_cast<char>(vel));
                std::string off; off.push_back(static_cast<char>(0x80)); off.push_back(static_cast<char>(n.pitch & 0x7f)); off.push_back('\0');
                ev.push_back({ n.tick, 2, on }); ev.push_back({ n.end(), 0, off });
            }
            out += track_chunk(std::move(ev), t->name);
        }
        return R::success(std::move(out));
    }

    Status write_midi_file(const fs::path& path, const Project& project, const MidiExportOptions& options) {
        auto r = encode_midi(project, options);
        if (!r) return Status::fail(r.error);
        return write_text_file_atomic(path, *r);
    }
}
