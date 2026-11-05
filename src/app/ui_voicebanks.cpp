// Voicebank manager: browse, inspect, select, preview, import, export, remove, locate.
#include "app.hpp"
#include <algorithm>

namespace melovio::ui {
    namespace {
        std::string g_selected_key;            // "<id>|<version>|<root>"
        bool g_replace = false;
        std::string g_remove_key;

        std::string key_of(const Voicebank& v) { return v.manifest.id + "|" + v.manifest.version + "|" + path_to_utf8(v.root); }

        const Voicebank* find_by_key(const App& a, const std::string& k) { for (auto& v : a.lib.all()) if (key_of(v) == k) return &v; return nullptr; }

        ImVec4 sev_color(Severity s) { return s == Severity::Error ? ImVec4(1, 0.4f, 0.35f, 1) : s == Severity::Warning ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.7f, 0.75f, 0.85f, 1); }

        // Audition: a short melody on the voicebank's own preview lyric, rendered by the real engine.
        void preview_voicebank(App& a, const Voicebank& vb) {
            if (!a.player) return;
            Project p; p.sample_rate = a.project.sample_rate;
            p.tempo_map.set_tempi({ { 0, 100.0 } });
            Track& t = p.add_track("Preview", vb.manifest.id);
            auto units = split_lyric_units(vb.manifest.preview_lyric);
            static const int steps[] = { 0, 0, 2, 4, 2, 0, -2, 0, 4, 4, 5, 7 };
            const int base = vb.profile_ok ? (vb.profile.range_low + vb.profile.range_high) / 2 : 62;
            Tick tick = 0; size_t i = 0;
            for (auto& u : units) { Note n; n.tick = tick; n.length = p.tempo_map.ppq() / 2; n.pitch = base + steps[i++ % 12]; n.lyric = u; p.add_note(t, n); tick += n.length; }
            if (t.notes.empty()) { a.toast("This voicebank has no preview lyric", Severity::Warning); return; }
            std::map<std::string, Voicebank> m = a.banks; m[vb.manifest.id] = vb;
            a.player->update_voicebanks(std::move(m));
            a.player->transport().set_loop(false, 0, 0);
            a.player->preview_async(p, 0.0, p.tempo_map.tick_to_sec(tick) + 0.8);
        }
    }

    void draw_voicebanks(App& a) {
        if (!a.show_voicebanks) return;
        ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Voicebanks", &a.show_voicebanks)) { ImGui::End(); return; }

        if (ImGui::Button("Import package (.mvb)...")) {
            a.file_dialog.show("vbimport", "Import voicebank package", FileMode::Open, { ".mvb" }, a.cfg.last_import_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_import_dir),
                "", [&a](const fs::path& p) {
                    a.cfg.last_import_dir = path_to_utf8(p.parent_path());
                    std::vector<Diagnostic> w; auto r = a.lib.install(p, g_replace, &w);
                    if (!r) a.toast("Import failed: " + r.error, Severity::Error, 12);
                    else { a.toast("Imported " + (*r)->manifest.name + " " + (*r)->manifest.version); for (auto& d : w) a.add_log(d.message); g_selected_key = key_of(**r); a.refresh_voicebanks(); }
                });
        }
        ImGui::SameLine();
        if (ImGui::Button("Import folder...")) {
            a.file_dialog.show("vbimportdir", "Import voicebank folder (contains voicebank.json)", FileMode::Folder, {}, a.cfg.last_import_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_import_dir),
                "", [&a](const fs::path& p) {
                    std::vector<Diagnostic> w; auto r = a.lib.install(p, g_replace, &w);
                    if (!r) a.toast("Import failed: " + r.error, Severity::Error, 12);
                    else { a.toast("Imported " + (*r)->manifest.name + " " + (*r)->manifest.version); for (auto& d : w) a.add_log(d.message); g_selected_key = key_of(**r); a.refresh_voicebanks(); }
                });
        }
        ImGui::SameLine(); ImGui::Checkbox("Replace same version", &g_replace);
        ImGui::SameLine(); if (ImGui::Button("Rescan")) { a.lib.refresh(); a.refresh_voicebanks(); a.toast("Voicebank library rescanned"); }
        ImGui::SameLine(); ImGui::TextDisabled("Imported to: %s", path_to_utf8(a.lib.user_dir()).c_str());

        ImGui::BeginChild("list", ImVec2(ImGui::GetContentRegionAvail().x * 0.30f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (a.lib.all().empty()) ImGui::TextDisabled("No voicebanks found. Import one or check the bundled voicebanks folder.");
        for (auto& v : a.lib.all()) {
            const std::string k = key_of(v);
            const bool bad = !v.usable();
            ImGui::PushStyleColor(ImGuiCol_Text, bad ? ImVec4(1, 0.45f, 0.4f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            std::string label = v.manifest.name + "  " + v.manifest.version;
            if (!v.manifest.native_name.empty()) label += "  " + v.manifest.native_name;
            if (ImGui::Selectable(label.c_str(), g_selected_key == k)) g_selected_key = k;
            ImGui::PopStyleColor();
            ImGui::SameLine(); ImGui::TextDisabled("%s", v.bundled ? "bundled" : "user");
            if (bad) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "unusable"); }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        const Voicebank* vb = find_by_key(a, g_selected_key);
        if (!vb && !a.lib.all().empty()) { vb = &a.lib.all().front(); g_selected_key = key_of(*vb); }
        if (vb) {
            const auto& m = vb->manifest;
            ImGui::Text("%s  %s", m.name.c_str(), m.native_name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("v%s", m.version.c_str());
            if (!m.tagline.empty()) ImGui::TextDisabled("%s", m.tagline.c_str());
            ImGui::Separator();
            const Track* ct = a.cur_track();
            if (ImGui::Button("Use for current track") && ct) {
                if (!vb->usable()) a.toast("This voicebank is unusable (see problems below)", Severity::Error);
                else { a.edit("Change voicebank"); Track* t = a.cur_track(); t->voicebank_id = m.id; t->persona.clear(); a.project.ensure_voicebank_ref(m.id, m.version, m.name); a.refresh_voicebanks(); a.analysis_cache.clear(); }
            }
            ImGui::SameLine(); ImGui::BeginDisabled(!vb->usable() || !a.player || a.player->status() == PlayerStatus::Rendering);
            if (ImGui::Button("Preview")) preview_voicebank(a, *vb);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Sings \"%s\" with the real synthesis engine.", m.preview_lyric.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Export .mvb...")) {
                const Voicebank copy = *vb;
                a.file_dialog.show("vbexport", "Export voicebank package", FileMode::Save, { ".mvb" }, a.cfg.last_export_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_export_dir), m.id + "-" + m.version + ".mvb",
                    [&a, copy](const fs::path& p) { Status s = a.lib.export_package(copy, p); a.toast(s ? "Exported " + path_to_utf8(p.filename()) : "Export failed: " + s.message, s ? Severity::Info : Severity::Error, 8); });
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(vb->bundled);
            if (ImGui::Button("Remove...")) { g_remove_key = key_of(*vb); ImGui::OpenPopup("confirm_remove"); }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && vb->bundled) ImGui::SetTooltip("Bundled voicebanks are read-only.");
            if (ImGui::BeginPopupModal("confirm_remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Remove %s %s from your voicebank folder?\nProjects using it will report it as missing.", m.name.c_str(), m.version.c_str());
                if (ImGui::Button("Remove")) {
                    Status s = a.lib.remove(m.id, m.version);
                    a.toast(s ? "Removed" : "Remove failed: " + s.message, s ? Severity::Info : Severity::Error);
                    g_selected_key.clear(); a.refresh_voicebanks(); ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Spacing();
            if (ImGui::BeginTable("kv", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 130); ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                auto row = [](const char* k, const std::string& v) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", v.c_str()); };
                row("ID", m.id); row("Author", m.author); row("License", m.license); row("Languages", [&] { std::string s; for (auto& l : m.languages) s += (s.empty() ? "" : ", ") + l; return s; }());
                row("Engine", m.engine + " " + m.engine_version + " (" + m.kind + ")"); row("Location", path_to_utf8(vb->root)); row("Requires Melovio", ">= " + m.min_melovio);
                if (vb->profile_ok) {
                    row("Comfortable range", std::string(pitch_name(vb->profile.range_low)) + " - " + pitch_name(vb->profile.range_high));
                    row("Hard range", std::string(pitch_name(vb->profile.hard_low)) + " - " + pitch_name(vb->profile.hard_high));
                    std::string ps; for (auto& p : vb->profile.personas) ps += (ps.empty() ? "" : ", ") + p; row("Personas", ps.empty() ? "none" : ps);
                }
                row("Phonetic mappings", std::to_string(m.phonetic_mappings.size())); row("Recordings", std::to_string(m.recordings.size()));
                ImGui::EndTable();
            }
            if (!m.description.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", m.description.c_str()); }
            if (!m.completeness.empty()) { ImGui::SeparatorText("What is in this voicebank"); ImGui::TextWrapped("%s", m.completeness.c_str()); }
            // compatibility with the current project
            if (ct && vb->profile_ok && !ct->notes.empty()) {
                int lo = 127, hi = 0; for (auto& n : ct->notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
                ImGui::SeparatorText("Compatibility with the current track");
                const bool hard = lo < vb->profile.hard_low || hi > vb->profile.hard_high, soft = lo < vb->profile.range_low || hi > vb->profile.range_high;
                ImGui::TextColored(hard ? sev_color(Severity::Error) : soft ? sev_color(Severity::Warning) : ImVec4(0.5f, 0.85f, 0.55f, 1), "Track notes span %s - %s: %s", pitch_name(lo), pitch_name(hi),
                                   hard ? "partly outside the hard range" : soft ? "partly outside the comfortable range" : "within the comfortable range");
            }
            // versions
            auto vers = a.lib.versions_of(m.id);
            if (vers.size() > 1) { ImGui::SeparatorText("Installed versions"); for (auto* v : vers) ImGui::BulletText("%s  (%s)", v->manifest.version.c_str(), v->bundled ? "bundled" : "user"); }
            if (!vb->diagnostics.empty()) { ImGui::SeparatorText("Validation"); for (auto& d : vb->diagnostics) ImGui::TextColored(sev_color(d.severity), "[%s] %s", d.code.c_str(), d.message.c_str()); }
            else if (vb->usable()) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "Validation passed.");
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
