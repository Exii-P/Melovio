// MIDI import/export dialogs.
#include "app.hpp"
#include "core/midi.hpp"
#include <algorithm>
#include <memory>

namespace melovio::ui {
    namespace {
        struct ImportState {
            bool open_request = false;
            std::unique_ptr<MidiFile> file;
            std::string source_name;
            std::vector<char> chosen;
            std::string voicebank_id;
            bool import_tempo = false;
            char default_lyric[32] = "あ";
        };
        ImportState I;

        struct ExportState { bool open_request = false; std::vector<char> chosen; bool include_muted = true; std::string path; };
        ExportState E;

        ImVec4 sev_color(Severity s) { return s == Severity::Error ? ImVec4(1, 0.4f, 0.35f, 1) : s == Severity::Warning ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.7f, 0.75f, 0.85f, 1); }

    }

    void load_midi_for_import(App& a, const fs::path& p) {
        a.cfg.last_import_dir = path_to_utf8(p.parent_path());
        auto r = read_midi_file(p);
        if (!r) { a.toast("Cannot import: " + r.error, Severity::Error, 10); return; }
        I.file = std::make_unique<MidiFile>(std::move(*r));
        I.source_name = path_to_utf8(p.filename());
        I.chosen.assign(I.file->tracks.size(), 1);
        I.import_tempo = a.project.end_tick() == 0;                  // an empty project takes the file's tempo by default
        const Track* t = a.cur_track(); I.voicebank_id = t ? t->voicebank_id : (a.lib.all().empty() ? "yune" : a.lib.all().front().manifest.id);
        std::snprintf(I.default_lyric, sizeof I.default_lyric, "%s", a.default_lyric.c_str());
        I.open_request = true;
    }

    void begin_midi_import(App& a) {
        a.file_dialog.show("midiimport", "Import MIDI file", FileMode::Open, { ".mid", ".midi", ".smf" }, a.cfg.last_import_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_import_dir), "",
            [&a](const fs::path& p) { load_midi_for_import(a, p); });
    }

    void begin_midi_export(App& a) {
        E.chosen.assign(a.project.tracks.size(), 1);
        E.path = path_to_utf8(path_from_utf8(a.project.meta.title.empty() ? "melovio" : a.project.meta.title).filename()) + ".mid";
        a.file_dialog.show("midiexport", "Export MIDI file", FileMode::Save, { ".mid" }, a.cfg.last_export_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_export_dir), E.path,
            [&a](const fs::path& p) {
                a.cfg.last_export_dir = path_to_utf8(p.parent_path());
                MidiExportOptions o; o.include_muted = true;
                for (size_t i = 0; i < a.project.tracks.size(); ++i) o.track_ids.push_back(a.project.tracks[i].id);
                fs::path out = p; if (out.extension().empty()) out += ".mid";
                Status s = write_midi_file(out, a.project, o);
                a.toast(s ? "Exported " + path_to_utf8(out.filename()) : "MIDI export failed: " + s.message, s ? Severity::Info : Severity::Error, 8);
            });
    }

    void draw_midi_dialogs(App& a) {
        if (I.open_request) { ImGui::OpenPopup("Import MIDI"); I.open_request = false; }
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Import MIDI", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        if (!I.file) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        const MidiFile& f = *I.file;
        ImGui::Text("%s", I.source_name.c_str());
        ImGui::TextDisabled("format %d, %d ticks per quarter note, %zu tempo event(s), %zu time-signature event(s)", f.format, f.ppq, f.tempi.size(), f.signatures.size());
        if (ImGui::BeginTable("miditracks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26); ImGui::TableSetupColumn("Track"); ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthFixed, 56);
            ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableSetupColumn("Lyrics", ImGuiTableColumnFlags_WidthFixed, 56); ImGui::TableHeadersRow();
            for (size_t i = 0; i < f.tracks.size(); ++i) {
                const MidiTrack& t = f.tracks[i]; int lo = 127, hi = 0; for (auto& n : t.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(i)); bool c = I.chosen[i] != 0; if (ImGui::Checkbox("##c", &c)) I.chosen[i] = c; ImGui::PopID();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(t.name.c_str()); ImGui::TableNextColumn(); ImGui::Text("%zu", t.notes.size());
                ImGui::TableNextColumn(); ImGui::Text("%s - %s", pitch_name(lo), pitch_name(hi)); ImGui::TableNextColumn(); ImGui::Text("%zu", t.lyric_count);
            }
            ImGui::EndTable();
        }
        const Voicebank* cur = nullptr; for (auto& v : a.lib.all()) if (v.manifest.id == I.voicebank_id) { cur = &v; break; }
        if (ImGui::BeginCombo("Voicebank for new tracks", cur ? (cur->manifest.name + " " + cur->manifest.version).c_str() : I.voicebank_id.c_str())) {
            for (auto& v : a.lib.all()) if (v.usable() && ImGui::Selectable((v.manifest.name + " " + v.manifest.version).c_str(), v.manifest.id == I.voicebank_id)) I.voicebank_id = v.manifest.id;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Replace the project's tempo and time signatures with the file's", &I.import_tempo);
        ImGui::SetNextItemWidth(80); ImGui::InputText("Lyric for notes without one", I.default_lyric, sizeof I.default_lyric);
        ImGui::TextWrapped("Voices are monophonic: chords keep their highest note and overlapping notes are trimmed. Lyrics are read from the file's Lyric events (UTF-8).");
        for (auto& d : f.diagnostics) ImGui::TextColored(sev_color(d.severity), "%s", d.message.c_str());
        ImGui::Separator();
        const bool any = std::any_of(I.chosen.begin(), I.chosen.end(), [](char c) { return c != 0; });
        ImGui::BeginDisabled(!any);
        if (ImGui::Button("Import")) {
            MidiImportOptions o; o.voicebank_id = I.voicebank_id; o.default_lyric = I.default_lyric[0] ? I.default_lyric : "あ"; o.import_tempo = I.import_tempo;
            for (size_t i = 0; i < I.chosen.size(); ++i) if (I.chosen[i]) o.tracks.push_back(i);
            a.edit("Import MIDI");
            std::vector<Diagnostic> d; const size_t first_new = a.project.tracks.size();
            auto r = import_midi(a.project, f, o, &d);
            if (!r) { a.toast("Import failed: " + r.error, Severity::Error, 10); }
            else {
                if (const Voicebank* v = cur) a.project.ensure_voicebank_ref(v->manifest.id, v->manifest.version, v->manifest.name);
                a.track_index = static_cast<int>(first_new); a.selection.clear(); a.analysis_cache.clear(); a.refresh_voicebanks(); a.touch();
                for (auto& x : d) a.add_log("midi import: " + x.message);
                a.toast("Imported " + std::to_string(r->tracks_added) + " track(s), " + std::to_string(r->notes_added) + " note(s)" + (r->notes_lost_to_overlap ? " (" + std::to_string(r->notes_lost_to_overlap) + " dropped: overlaps/chords)" : ""), r->notes_lost_to_overlap ? Severity::Warning : Severity::Info, 8);
            }
            I.file.reset(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { I.file.reset(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
