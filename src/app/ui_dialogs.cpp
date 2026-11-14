// Modal dialogs: export, project settings, recovery, missing voicebanks, unsaved changes, about, log, toasts.
#include "app.hpp"
#include <algorithm>
#include <cmath>

namespace melovio::ui {
    namespace {
        ImVec4 sev_color(Severity s) { return s == Severity::Error ? ImVec4(1, 0.4f, 0.35f, 1) : s == Severity::Warning ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.7f, 0.75f, 0.85f, 1); }

        void draw_export(App& a) {
            if (a.open_export) { ImGui::OpenPopup("Export audio"); a.open_export = false; if (a.export_path_text.empty()) a.export_path_text = path_to_utf8((a.cfg.last_export_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_export_dir)) / path_from_utf8(a.project.meta.title + ".wav")); }
            ImGui::SetNextWindowSize(ImVec2(520, 0));
            if (!ImGui::BeginPopupModal("Export audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ExportSettings& s = a.export_settings; ExportJob& job = a.export_job;
            const bool busy = job.running;
            ImGui::BeginDisabled(busy);
            int scope = static_cast<int>(s.scope);
            ImGui::RadioButton("Whole project", &scope, 0); ImGui::SameLine(); ImGui::RadioButton("Region", &scope, 1); ImGui::SameLine(); ImGui::RadioButton("Single track", &scope, 2);
            s.scope = static_cast<ExportScope>(scope);
            if (s.scope == ExportScope::Region) {
                int r0 = static_cast<int>(s.region_start), r1 = static_cast<int>(s.region_end);
                ImGui::SetNextItemWidth(110); if (ImGui::InputInt("Start tick", &r0)) s.region_start = std::max(0, r0);
                ImGui::SameLine(); ImGui::SetNextItemWidth(110); if (ImGui::InputInt("End tick", &r1)) s.region_end = std::max(0, r1);
                if (ImGui::Button("Use loop region") && a.project.loop.end > a.project.loop.start) { s.region_start = a.project.loop.start; s.region_end = a.project.loop.end; }
                ImGui::SameLine(); if (ImGui::Button("Use selection")) { auto sel = a.selected_notes(); if (!sel.empty()) { s.region_start = sel.front()->tick; s.region_end = sel.front()->end(); for (auto* n : sel) { s.region_start = std::min(s.region_start, n->tick); s.region_end = std::max(s.region_end, n->end()); } } }
                ImGui::SameLine(); ImGui::TextDisabled("%s - %s", tick_label(a.project.tempo_map, s.region_start).c_str(), tick_label(a.project.tempo_map, s.region_end).c_str());
            }
            if (s.scope == ExportScope::Track) {
                if (s.track_id.empty() || !a.project.find_track(s.track_id)) { if (auto* t = a.cur_track()) s.track_id = t->id; }
                const Track* ct = a.project.find_track(s.track_id);
                if (ImGui::BeginCombo("Track", ct ? ct->name.c_str() : "")) { for (auto& t : a.project.tracks) if (ImGui::Selectable(t.name.c_str(), t.id == s.track_id)) s.track_id = t.id; ImGui::EndCombo(); }
                ImGui::TextDisabled("A single track is exported on its own (mute/solo ignored), with its gain and pan.");
            }
            static const char* srn[] = { "22050 Hz", "32000 Hz", "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" };
            int si = 2; for (int i = 0; i < 6; ++i) if (kExportSampleRates[i] == s.sample_rate) si = i;
            ImGui::SetNextItemWidth(140); if (ImGui::Combo("Sample rate", &si, srn, 6)) s.sample_rate = kExportSampleRates[si];
            ImGui::SameLine(); int bi = s.bit_depth == 16 ? 0 : s.bit_depth == 24 ? 1 : 2; static const char* bn[] = { "16-bit PCM", "24-bit PCM", "32-bit float" };
            ImGui::SetNextItemWidth(140); if (ImGui::Combo("Bit depth", &bi, bn, 3)) s.bit_depth = bi == 0 ? 16 : bi == 1 ? 24 : 32;
            int ch = s.channels == 1 ? 0 : 1; static const char* cn[] = { "Mono", "Stereo" };
            ImGui::SetNextItemWidth(140); if (ImGui::Combo("Channels", &ch, cn, 2)) s.channels = ch == 0 ? 1 : 2;
            ImGui::Checkbox("Normalize", &s.normalize); if (s.normalize) { ImGui::SameLine(); ImGui::SetNextItemWidth(120); float pk = static_cast<float>(s.normalize_peak_db); if (ImGui::SliderFloat("Peak dBFS", &pk, -12.f, 0.f, "%.1f")) s.normalize_peak_db = pk; }
            if (s.bit_depth != 32) { ImGui::SameLine(); ImGui::Checkbox("TPDF dither", &s.dither); }
            if (s.scope != ExportScope::Region) { float tail = static_cast<float>(s.tail_sec); ImGui::SetNextItemWidth(140); if (ImGui::SliderFloat("Tail (s)", &tail, 0.f, 5.f, "%.1f")) s.tail_sec = tail; }
            char pathbuf[1024]; std::snprintf(pathbuf, sizeof pathbuf, "%s", a.export_path_text.c_str());
            ImGui::SetNextItemWidth(-90); if (ImGui::InputText("##outpath", pathbuf, sizeof pathbuf)) a.export_path_text = pathbuf;
            ImGui::SameLine(); if (ImGui::Button("Browse...")) {
                a.file_dialog.show("exportpath", "Export to", FileMode::Save, { ".wav" }, a.cfg.last_export_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_export_dir), a.project.meta.title + ".wav", [&a](const fs::path& p) { a.export_path_text = path_to_utf8(p); a.cfg.last_export_dir = path_to_utf8(p.parent_path()); a.open_export = true; });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            auto diags = validate_export_settings(s, a.project);
            for (auto& d : diags) ImGui::TextColored(sev_color(d.severity), "%s", d.message.c_str());
            for (auto& r : a.resolutions) if (!r.ok()) { ImGui::TextColored(sev_color(Severity::Warning), "Voicebank \"%s\" is missing: its notes will be skipped.", r.id.c_str()); }
            ImGui::Separator();
            if (busy) {
                ImGui::ProgressBar(static_cast<float>(job.progress.load()), ImVec2(-1, 0));
                if (ImGui::Button("Cancel export")) job.cancel.cancel();
            } else {
                const bool can = !has_errors(diags) && !a.export_path_text.empty();
                ImGui::BeginDisabled(!can);
                if (ImGui::Button("Export")) {
                    fs::path out = path_from_utf8(a.export_path_text); if (out.extension().empty()) out += ".wav";
                    std::error_code ec;
                    if (fs::exists(out, ec)) ImGui::OpenPopup("overwrite?"); else a.start_export(out);
                }
                ImGui::EndDisabled();
                if (ImGui::BeginPopupModal("overwrite?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("%s already exists. Replace it?", path_to_utf8(path_from_utf8(a.export_path_text).filename()).c_str());
                    if (ImGui::Button("Replace")) { a.start_export(path_from_utf8(a.export_path_text)); ImGui::CloseCurrentPopup(); }
                    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                if (job.finished) {
                    std::lock_guard<std::mutex> l(job.m);
                    ImGui::TextColored(job.ok ? ImVec4(0.5f, 0.85f, 0.55f, 1) : sev_color(Severity::Error), "%s", job.result_text.c_str());
                    for (auto& d : job.diags) ImGui::TextColored(sev_color(d.severity), "%s", d.message.c_str());
                }
            }
            ImGui::EndPopup();
        }

        struct SettingsState { ProjectMeta meta; std::vector<TempoChange> tempi; std::vector<TimeSigChange> sigs; int sr = 44100; };
        SettingsState S;
        void draw_settings(App& a) {
            if (a.open_settings) { S = { a.project.meta, a.project.tempo_map.tempi(), a.project.tempo_map.signatures(), a.project.sample_rate }; ImGui::OpenPopup("Project settings"); a.open_settings = false; }
            ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal("Project settings", nullptr)) return;
            auto text = [](const char* l, std::string& v, int cap = 512) { std::vector<char> b(static_cast<size_t>(cap), 0); std::snprintf(b.data(), b.size(), "%s", v.c_str()); if (ImGui::InputText(l, b.data(), b.size())) v = b.data(); };
            ImGui::SeparatorText("Metadata");
            text("Title", S.meta.title); text("Composer", S.meta.composer); text("Lyricist", S.meta.lyricist); text("Comment", S.meta.comment, 2048);
            ImGui::TextDisabled("Created %s   Modified %s", S.meta.created.c_str(), S.meta.modified.c_str());
            ImGui::SeparatorText("Tempo");
            for (size_t i = 0; i < S.tempi.size(); ++i) {
                ImGui::PushID(static_cast<int>(i)); int tk = static_cast<int>(S.tempi[i].tick); float bpm = static_cast<float>(S.tempi[i].bpm);
                ImGui::SetNextItemWidth(100); ImGui::BeginDisabled(i == 0); if (ImGui::InputInt("tick", &tk, 0)) S.tempi[i].tick = std::max(0, tk); ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::SetNextItemWidth(100); if (ImGui::InputFloat("BPM", &bpm, 0, 0, "%.2f")) S.tempi[i].bpm = std::clamp<double>(bpm, 20.0, 400.0);
                ImGui::SameLine(); if (i > 0 && ImGui::SmallButton("remove")) { S.tempi.erase(S.tempi.begin() + static_cast<long>(i)); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ tempo change")) S.tempi.push_back({ S.tempi.back().tick + a.project.tempo_map.ppq() * 4, S.tempi.back().bpm });
            ImGui::SeparatorText("Time signature");
            for (size_t i = 0; i < S.sigs.size(); ++i) {
                ImGui::PushID(1000 + static_cast<int>(i)); int bar = S.sigs[i].bar + 1, num = S.sigs[i].numerator, den = S.sigs[i].denominator;
                ImGui::SetNextItemWidth(90); ImGui::BeginDisabled(i == 0); if (ImGui::InputInt("from bar", &bar, 0)) S.sigs[i].bar = std::max(0, bar - 1); ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::SetNextItemWidth(70); if (ImGui::InputInt("##n", &num, 0)) S.sigs[i].numerator = std::clamp(num, 1, 32);
                ImGui::SameLine(); ImGui::Text("/"); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
                static const int dens[] = { 1, 2, 4, 8, 16 }; int di = 2; for (int k = 0; k < 5; ++k) if (dens[k] == den) di = k;
                char dl[8]; std::snprintf(dl, sizeof dl, "%d", dens[di]);
                if (ImGui::BeginCombo("##d", dl)) { for (int k = 0; k < 5; ++k) { char b[8]; std::snprintf(b, sizeof b, "%d", dens[k]); if (ImGui::Selectable(b, k == di)) S.sigs[i].denominator = dens[k]; } ImGui::EndCombo(); }
                ImGui::SameLine(); if (i > 0 && ImGui::SmallButton("remove")) { S.sigs.erase(S.sigs.begin() + static_cast<long>(i)); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ signature change")) S.sigs.push_back({ S.sigs.back().bar + 4, S.sigs.back().numerator, S.sigs.back().denominator });
            ImGui::SeparatorText("Application");
            int as = a.cfg.autosave_seconds; ImGui::SetNextItemWidth(120); if (ImGui::InputInt("Autosave every (s, 0 = off)", &as)) a.cfg.autosave_seconds = std::clamp(as, 0, 3600);
            ImGui::TextDisabled("Autosaves go to a separate recovery folder and never overwrite your project.");
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                a.edit("Project settings");
                a.project.meta = S.meta; a.project.tempo_map.set_tempi(S.tempi); a.project.tempo_map.set_signatures(S.sigs);
                a.touch(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void draw_recovery(App& a) {
            if (a.open_recovery) { ImGui::OpenPopup("Recover unsaved work"); a.open_recovery = false; }
            if (!ImGui::BeginPopupModal("Recover unsaved work", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextWrapped("Melovio found autosaved work from a session that did not close normally. Recovering opens the autosave as an unsaved copy; your original file is untouched.");
            for (size_t i = 0; i < a.recovery_candidates.size(); ++i) {
                auto c = a.recovery_candidates[i];
                ImGui::PushID(static_cast<int>(i)); ImGui::Separator();
                ImGui::Text("%s", c.original_path.empty() ? "(never saved project)" : c.original_path.c_str()); ImGui::TextDisabled("autosaved %s", c.modified.c_str());
                if (ImGui::Button("Recover")) {
                    std::vector<Diagnostic> w; auto r = a.autosave->recover(c, &w);
                    if (!r) a.toast("Recovery failed: " + r.error, Severity::Error, 10);
                    else { a.adopt_project(std::move(*r), {}); a.dirty = true; a.toast("Recovered. Use Save As to keep it."); a.recovery_candidates.clear(); ImGui::PopID(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
                }
                ImGui::SameLine(); if (ImGui::Button("Discard")) { a.autosave->discard(c); a.recovery_candidates.erase(a.recovery_candidates.begin() + static_cast<long>(i)); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            ImGui::Separator(); if (ImGui::Button("Decide later") || a.recovery_candidates.empty()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void draw_missing(App& a) {
            if (a.open_missing) { ImGui::OpenPopup("Missing voicebanks"); a.open_missing = false; }
            if (!ImGui::BeginPopupModal("Missing voicebanks", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextWrapped("This project needs voicebanks that are not usable on this computer. Notes on those tracks will not sound until you locate or import them (Voicebanks panel).");
            bool any = false;
            for (auto& r : a.resolutions) {
                if (r.ok()) continue;
                any = true; ImGui::PushID(r.id.c_str()); ImGui::Separator();
                ImGui::TextColored(sev_color(Severity::Error), "%s", r.id.c_str()); ImGui::TextWrapped("%s", r.message.c_str());
                if (ImGui::Button("Locate folder...")) {
                    const std::string id = r.id;
                    a.file_dialog.show("locate", "Locate voicebank \"" + id + "\"", FileMode::Folder, {}, a.cfg.last_import_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_import_dir), "", [&a, id](const fs::path& d) {
                        const fs::path pd = a.project_path.empty() ? fs::current_path() : a.project_path.parent_path();
                        a.edit("Locate voicebank"); Status s = a.lib.locate(a.project, id, d, pd);
                        if (!s) a.toast("Locate failed: " + s.message, Severity::Error, 10); else { a.toast("Voicebank located"); a.refresh_voicebanks(); }
                    });
                }
                ImGui::PopID();
            }
            if (!any) ImGui::CloseCurrentPopup();
            ImGui::Separator(); if (ImGui::Button("Continue")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void draw_unsaved(App& a) {
            if (a.open_new_confirm) { ImGui::OpenPopup("Unsaved changes"); a.open_new_confirm = false; }
            if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::Text("Save changes to \"%s\" first?", a.project.meta.title.c_str());
            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup();
                if (a.save_project_now()) { auto f = std::move(a.after_unsaved); a.after_unsaved = nullptr; if (f) f(); }   // no path: Save As runs the continuation itself
            }
            ImGui::SameLine(); if (ImGui::Button("Don't save")) { ImGui::CloseCurrentPopup(); a.dirty = false; auto f = std::move(a.after_unsaved); a.after_unsaved = nullptr; if (f) f(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) { a.after_unsaved = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        void draw_about(App& a) {
            if (a.open_about) { ImGui::OpenPopup("About Melovio"); a.open_about = false; }
            if (!ImGui::BeginPopupModal("About Melovio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::Text("Melovio Studio %s", MELOVIO_VERSION);
            ImGui::TextDisabled("Japanese singing-voice synthesis editor");
            const EngineInfo ei = engine_info();
            ImGui::Text("Synthesis backend: %s %s (%s)", ei.name.c_str(), ei.version.c_str(), ei.kind.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("Melovio Studio is the application. Wauvio-Vocal is the synthesis backend, bundled unmodified and used through a narrow bridge. Voices are generated by a parametric formant model: there are no recordings or samples anywhere in this program.");
            ImGui::TextWrapped("Libraries: Dear ImGui, SDL2, nlohmann/json, miniz, doctest (tests).");
            ImGui::Text("Config: %s", path_to_utf8(a.cfg_path).c_str());
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void draw_log(App& a) {
            if (!a.show_log) return;
            ImGui::SetNextWindowSize(ImVec2(640, 260), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Log & Diagnostics", &a.show_log)) {
                if (ImGui::BeginTabBar("##logtabs")) {
                    if (ImGui::BeginTabItem("Project diagnostics")) {
                        if (a.diagnostics.empty()) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "No problems found.");
                        for (auto& d : a.diagnostics) ImGui::TextColored(sev_color(d.severity), "[%s] %s", d.code.c_str(), d.message.c_str());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Log")) { for (auto& l : a.log) ImGui::TextUnformatted(l.c_str()); if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f); ImGui::EndTabItem(); }
                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }

        void draw_toasts(App& a) {
            while (!a.toasts.empty() && a.toasts.front().until < a.now) a.toasts.pop_front();
            const ImGuiViewport* vp = ImGui::GetMainViewport(); float y = vp->WorkPos.y + vp->WorkSize.y - 12;
            int i = 0;
            for (auto it = a.toasts.rbegin(); it != a.toasts.rend(); ++it, ++i) {
                ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12, y), ImGuiCond_Always, ImVec2(1, 1));
                ImGui::SetNextWindowBgAlpha(0.92f);
                ImGui::PushStyleColor(ImGuiCol_Border, sev_color(it->sev));
                ImGui::Begin(("##toast" + std::to_string(i)).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
                ImGui::PushTextWrapPos(380); ImGui::TextUnformatted(it->text.c_str()); ImGui::PopTextWrapPos();
                y -= ImGui::GetWindowSize().y + 6;
                ImGui::End(); ImGui::PopStyleColor();
            }
        }
    }

    void draw_dialogs(App& a) {
        draw_export(a); draw_settings(a); draw_recovery(a); draw_missing(a); draw_unsaved(a); draw_about(a);
        draw_midi_dialogs(a); draw_log(a); draw_toasts(a);
        a.file_dialog.draw();
        if (a.show_demo) ImGui::ShowDemoWindow(&a.show_demo);
    }
}