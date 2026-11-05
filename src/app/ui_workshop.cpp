// Voicebank workshop: create and tune parametric voicebanks (copy -> edit manifest and voice profile -> test -> validate -> install).
// Wauvio-Vocal is a parametric formant synthesiser: there are no recordings to cut or label, so this workspace edits the voice model,
// not samples. That limit is stated in the UI on purpose.
#include "app.hpp"
#include <algorithm>
#include <cmath>
#include <future>

namespace melovio::ui {
    namespace {
        struct Workshop {
            bool loaded = false;
            fs::path dir;
            VoicebankManifest m;
            ProfileParams p;
            bool modified = false;
            std::string status;
            char new_id[64] = "my-voice", new_name[96] = "My Voice";
            int source_index = 0;
            // test render
            std::future<std::vector<float>> render_job; bool rendering = false;
            std::vector<float> wave;                 // min/max pairs for display
            double wave_sec = 0;
            std::string render_error;
            // completeness
            std::future<CompletenessReport> check_job; bool checking = false;
            CompletenessReport report; bool have_report = false;
            char test_lyric[128] = "さくらさくら";
        };
        Workshop W;

        template <class F> bool field(const char* label, std::string& s, F&& on_change, int cap = 256) {
            std::vector<char> b(static_cast<size_t>(cap), 0); std::snprintf(b.data(), b.size(), "%s", s.c_str());
            if (ImGui::InputText(label, b.data(), b.size())) { s = b.data(); on_change(); return true; }
            return false;
        }

        fs::path workshop_root() { return default_user_data_dir() / "workshop"; }

        Project test_project(const Voicebank& vb, const std::string& lyric) {
            Project p; p.tempo_map.set_tempi({ { 0, 100.0 } });
            Track& t = p.add_track("Test", vb.manifest.id);
            static const int steps[] = { 0, 0, 2, 4, 2, 0, -2, 0, 4, 4, 5, 7 };
            const int base = vb.profile_ok ? (vb.profile.range_low + vb.profile.range_high) / 2 : 62;
            Tick tick = 0; size_t i = 0;
            for (auto& u : split_lyric_units(lyric)) { Note n; n.tick = tick; n.length = p.tempo_map.ppq() / 2; n.pitch = base + steps[i++ % 12]; n.lyric = u; p.add_note(t, n); tick += n.length; }
            return p;
        }

        bool save_workshop(App& a) {
            Status s = write_text_file_atomic(W.dir / kVoicebankManifestName, manifest_to_json(W.m));
            if (!s) { W.status = "Save failed: " + s.message; return false; }
            s = write_profile(W.p, W.dir / path_from_utf8(W.m.profile));
            if (!s) { W.status = "Saved the manifest but the voice profile failed: " + s.message; return false; }
            W.modified = false; W.status = "Saved to " + path_to_utf8(W.dir); a.add_log("workshop: " + W.status);
            return true;
        }

        void load_workshop(const fs::path& dir) {
            auto r = load_voicebank(dir, false);
            if (!r) { W.status = "Cannot load: " + r.error; return; }
            W.m = r->manifest; W.dir = dir;
            auto pr = read_profile(dir / path_from_utf8(r->manifest.profile));
            if (!pr) { W.status = "Voice profile error: " + pr.error; return; }
            W.p = *pr; W.loaded = true; W.modified = false; W.have_report = false; W.wave.clear(); W.status = "Opened " + path_to_utf8(dir);
        }

        void create_from(App& a, const Voicebank& src) {
            std::string id = W.new_id; std::string err;
            for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) err = "id may only contain a-z 0-9 - _";
            if (id.empty()) err = "id is empty";
            const fs::path dest = workshop_root() / id;
            std::error_code ec;
            if (err.empty() && fs::exists(dest, ec)) err = "a workshop voicebank with this id already exists: " + path_to_utf8(dest);
            if (err.empty() && !a.lib.versions_of(id).empty()) err = "a voicebank with this id is already installed; pick a different id";
            if (!err.empty()) { W.status = err; return; }
            fs::create_directories(dest, ec);
            fs::copy(src.root, dest, fs::copy_options::recursive, ec);
            if (ec) { W.status = "Copy failed: " + ec.message(); return; }
            VoicebankManifest m = src.manifest; m.id = id; m.name = W.new_name; m.native_name.clear(); m.version = "0.1.0"; m.recordings.clear();
            m.description = "Derived from " + src.manifest.name + " " + src.manifest.version + " in the Melovio workshop.";
            m.completeness = "Parametric voice derived from " + src.manifest.name + "; no recordings. Not yet checked with the deep validation.";
            Status s = write_text_file_atomic(dest / kVoicebankManifestName, manifest_to_json(m));
            if (!s) { W.status = "Cannot write manifest: " + s.message; return; }
            load_workshop(dest);
        }

        void draw_wave(float h) {
            const ImVec2 p0 = ImGui::GetCursorScreenPos(); const float w = std::max(100.f, ImGui::GetContentRegionAvail().x);
            ImGui::Dummy(ImVec2(w, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(22, 24, 30, 255));
            dl->AddLine(ImVec2(p0.x, p0.y + h / 2), ImVec2(p0.x + w, p0.y + h / 2), IM_COL32(60, 64, 76, 255));
            const size_t cols = W.wave.size() / 2; if (!cols) { dl->AddText(ImVec2(p0.x + 8, p0.y + 6), IM_COL32(130, 135, 150, 255), "Render the test phrase to see its waveform"); return; }
            for (size_t i = 0; i < cols; ++i) {
                const float x = p0.x + w * static_cast<float>(i) / static_cast<float>(cols);
                dl->AddLine(ImVec2(x, p0.y + h / 2 - W.wave[2 * i + 1] * h / 2), ImVec2(x, p0.y + h / 2 - W.wave[2 * i] * h / 2 - 1), IM_COL32(110, 190, 250, 255));
            }
        }

        void poll_jobs(App& a) {
            if (W.rendering && W.render_job.valid() && W.render_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto s = W.render_job.get(); W.rendering = false; W.wave.clear();
                const size_t cols = 700; W.wave.resize(cols * 2, 0.f);
                float peak = 1e-6f; for (float v : s) peak = std::max(peak, std::fabs(v));
                for (size_t c = 0; c < cols && !s.empty(); ++c) {
                    const size_t i0 = c * s.size() / cols, i1 = std::max(i0 + 1, (c + 1) * s.size() / cols);
                    float lo = 0, hi = 0; for (size_t i = i0; i < i1 && i < s.size(); ++i) { lo = std::min(lo, s[i]); hi = std::max(hi, s[i]); }
                    W.wave[2 * c] = hi / std::max(1.f, peak); W.wave[2 * c + 1] = lo / std::max(1.f, peak);
                }
                W.wave_sec = static_cast<double>(s.size()) / 44100.0;
                if (s.empty() && W.render_error.empty()) W.render_error = "nothing was rendered";
            }
            if (W.checking && W.check_job.valid() && W.check_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                W.report = W.check_job.get(); W.checking = false; W.have_report = true;
                a.add_log("workshop: completeness " + std::to_string(W.report.passed) + "/" + std::to_string(W.report.total));
            }
        }
    }

    void draw_workshop(App& a) {
        if (!a.show_workshop) return;
        ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Voicebank Workshop", &a.show_workshop)) { ImGui::End(); return; }
        poll_jobs(a);

        ImGui::TextWrapped("Workshop voicebanks are parametric voices for the Wauvio-Vocal engine. There are no recordings to edit: you shape the voice model (formants, glottal source, range, character) and test it with the real synthesiser. Sample-based voicebanks are not supported by this engine.");
        ImGui::Separator();

        if (!W.loaded) {
            ImGui::SeparatorText("Start a new voicebank");
            const auto& all = a.lib.all();
            std::vector<const Voicebank*> usable; for (auto& v : all) if (v.usable()) usable.push_back(&v);
            if (usable.empty()) ImGui::TextDisabled("No usable voicebank to start from.");
            else {
                W.source_index = std::clamp(W.source_index, 0, static_cast<int>(usable.size()) - 1);
                const Voicebank& src = *usable[static_cast<size_t>(W.source_index)];
                if (ImGui::BeginCombo("Start from", (src.manifest.name + " " + src.manifest.version).c_str())) {
                    for (size_t i = 0; i < usable.size(); ++i) if (ImGui::Selectable((usable[i]->manifest.name + " " + usable[i]->manifest.version).c_str(), static_cast<int>(i) == W.source_index)) W.source_index = static_cast<int>(i);
                    ImGui::EndCombo();
                }
                ImGui::InputText("New id", W.new_id, sizeof W.new_id); ImGui::InputText("New name", W.new_name, sizeof W.new_name);
                if (ImGui::Button("Create working copy")) create_from(a, src);
            }
            ImGui::SeparatorText("Or open an existing workshop voicebank");
            if (ImGui::Button("Open folder...")) a.file_dialog.show("wsopen", "Open voicebank folder", FileMode::Folder, {}, fs::exists(workshop_root()) ? workshop_root() : fs::current_path(), "", [](const fs::path& p) { load_workshop(p); });
            ImGui::TextDisabled("Working copies live in %s", path_to_utf8(workshop_root()).c_str());
            if (!W.status.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", W.status.c_str());
            ImGui::End(); return;
        }

        ImGui::Text("Editing: %s  (%s)%s", W.m.name.c_str(), path_to_utf8(W.dir).c_str(), W.modified ? "  *modified" : "");
        if (ImGui::Button("Save")) save_workshop(a);
        ImGui::SameLine();
        if (ImGui::Button("Install into library")) {
            if (W.modified && !save_workshop(a)) { /* status set */ }
            else { std::vector<Diagnostic> w; auto r = a.lib.install(W.dir, true, &w); if (!r) W.status = "Install failed: " + r.error; else { W.status = "Installed " + (*r)->manifest.id + " " + (*r)->manifest.version; a.refresh_voicebanks(); } }
        }
        ImGui::SameLine(); if (ImGui::Button("Close")) { W.loaded = false; W.wave.clear(); }
        if (!W.status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", W.status.c_str()); }

        auto mod = [] { W.modified = true; };
        if (ImGui::BeginTabBar("wstabs")) {
            if (ImGui::BeginTabItem("Metadata")) {
                field("Name", W.m.name, mod); field("Native name", W.m.native_name, mod); field("Version", W.m.version, mod, 32);
                if (!valid_version(W.m.version)) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Version must look like 1.2.3");
                field("Author", W.m.author, mod); field("License", W.m.license, mod); field("Tagline", W.m.tagline, mod); field("Homepage", W.m.homepage, mod);
                field("Preview lyric", W.m.preview_lyric, mod); field("Description", W.m.description, mod, 1024); field("Character notes", W.m.character_notes, mod, 1024);
                field("Completeness statement", W.m.completeness, mod, 1024);
                ImGui::TextDisabled("Keep the completeness statement honest: it is shown to users of your voicebank.");
                ImGui::SeparatorText("Phonetic mappings (lyric -> pronunciation)");
                std::string erase_key;
                for (auto& [k, v] : W.m.phonetic_mappings) { ImGui::PushID(k.c_str()); ImGui::Text("%s  ->  %s", k.c_str(), v.c_str()); ImGui::SameLine(); if (ImGui::SmallButton("x")) erase_key = k; ImGui::PopID(); }
                if (!erase_key.empty()) { W.m.phonetic_mappings.erase(erase_key); mod(); }
                static char mk[64], mv[128];
                ImGui::SetNextItemWidth(120); ImGui::InputText("##mk", mk, sizeof mk); ImGui::SameLine(); ImGui::SetNextItemWidth(220); ImGui::InputText("##mv", mv, sizeof mv); ImGui::SameLine();
                if (ImGui::Button("Add mapping") && mk[0] && mv[0]) {
                    const std::string err = validate_phoneme_text(mv);
                    if (err.empty()) { std::string v = mv; if (v.front() != '{') v = "{" + v + "}"; W.m.phonetic_mappings[mk] = v; mk[0] = mv[0] = 0; mod(); } else W.status = "Mapping rejected: " + err;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Voice model")) {
                ImGui::TextDisabled("These values are written to voice.voice using the engine's own profile format.");
                int lo = W.p.range_low, hi = W.p.range_high, hl = W.p.hard_low, hh = W.p.hard_high;
                ImGui::SetNextItemWidth(80); if (ImGui::InputInt("Comfortable low", &lo)) { W.p.range_low = std::clamp(lo, 0, 127); mod(); }
                ImGui::SameLine(); ImGui::SetNextItemWidth(80); if (ImGui::InputInt("high", &hi)) { W.p.range_high = std::clamp(hi, 0, 127); mod(); }
                ImGui::SetNextItemWidth(80); if (ImGui::InputInt("Hard low", &hl)) { W.p.hard_low = std::clamp(hl, 0, 127); mod(); }
                ImGui::SameLine(); ImGui::SetNextItemWidth(80); if (ImGui::InputInt("hard high", &hh)) { W.p.hard_high = std::clamp(hh, 0, 127); mod(); }
                if (W.p.range_low > W.p.range_high || W.p.hard_low > W.p.range_low || W.p.hard_high < W.p.range_high) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Ranges should nest: hard low <= low <= high <= hard high.");
                if (ImGui::SliderFloat("Natural amount", &W.p.natural_amount, 0.f, 1.f)) mod();
                if (ImGui::Checkbox("Merge long vowels in lyrics", &W.p.parse_merge_long_vowels)) mod();
                ImGui::SameLine(); if (ImGui::Checkbox("Cute parsing", &W.p.parse_cute)) mod();
                ImGui::SeparatorText("Vowel formants (Hz / bandwidth)");
                static const char* vn[5] = { "a", "i", "u", "e", "o" };
                if (ImGui::BeginTable("vow", 8, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Borders)) {
                    const char* hdr[] = { "", "F1", "F2", "F3", "B1", "B2", "B3", "gain" }; for (auto h : hdr) ImGui::TableSetupColumn(h); ImGui::TableHeadersRow();
                    for (int v = 0; v < 5; ++v) {
                        VowelParams& vp = W.p.vowel[v]; ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(vn[v]);
                        float* f[7] = { &vp.f1, &vp.f2, &vp.f3, &vp.b1, &vp.b2, &vp.b3, &vp.gain };
                        for (int k = 0; k < 7; ++k) { ImGui::TableNextColumn(); ImGui::PushID(v * 10 + k); ImGui::SetNextItemWidth(-1); if (ImGui::DragFloat("##v", f[k], k == 6 ? 0.01f : 5.f, 0.f, k == 6 ? 4.f : 6000.f, k == 6 ? "%.2f" : "%.0f")) mod(); ImGui::PopID(); }
                    }
                    ImGui::EndTable();
                }
                // vowel chart (F1 vs F2), the classic acoustic phonetics view
                ImGui::SeparatorText("Vowel chart (F2 horizontal, F1 vertical)");
                const ImVec2 p0 = ImGui::GetCursorScreenPos(); const float cw = 260, ch = 140; ImGui::Dummy(ImVec2(cw, ch));
                ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(p0, ImVec2(p0.x + cw, p0.y + ch), IM_COL32(22, 24, 30, 255));
                for (int v = 0; v < 5; ++v) {
                    const float x = p0.x + cw - (std::clamp(W.p.vowel[v].f2, 600.f, 3000.f) - 600.f) / 2400.f * (cw - 20) - 10, y = p0.y + 10 + (std::clamp(W.p.vowel[v].f1, 200.f, 1000.f) - 200.f) / 800.f * (ch - 20);
                    dl->AddCircleFilled(ImVec2(x, y), 4, IM_COL32(110, 190, 250, 255)); dl->AddText(ImVec2(x + 6, y - 7), IM_COL32(220, 225, 240, 255), vn[v]);
                }
                auto edit_map = [&](const char* title, std::map<std::string, float>& mp) {
                    if (!ImGui::CollapsingHeader(title)) return;
                    for (auto& [k, v] : mp) { ImGui::PushID(k.c_str()); ImGui::SetNextItemWidth(160); if (ImGui::DragFloat(k.c_str(), &v, 0.01f * std::max(1.f, std::fabs(v)))) mod(); ImGui::PopID(); }
                };
                edit_map("Engine model values", W.p.model); edit_map("Engine tuning values", W.p.tune); edit_map("Default style", W.p.style);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Test & validate")) {
                ImGui::InputText("Test lyric", W.test_lyric, sizeof W.test_lyric);
                ImGui::TextDisabled("Test renders use the saved files; press Save first after editing.");
                ImGui::BeginDisabled(W.rendering || W.modified);
                if (ImGui::Button("Render test phrase")) {
                    auto lv = load_voicebank(W.dir, false);
                    if (!lv) W.render_error = lv.error;
                    else {
                        W.render_error.clear(); W.rendering = true;
                        Voicebank vb = *lv; std::string lyric = W.test_lyric; auto pe = a.pron;
                        W.render_job = std::async(std::launch::async, [vb, lyric, pe]() -> std::vector<float> {
                            Project p = test_project(vb, lyric); if (p.tracks.empty() || p.tracks[0].notes.empty()) return {};
                            Renderer r({ { vb.manifest.id, vb } }, std::make_shared<PhraseCache>(), std::make_shared<PronunciationEngine>(pe));
                            RenderOptions o; o.sample_rate = 44100; auto res = r.render_track(p, p.tracks[0], o);
                            return res ? *res : std::vector<float>{};
                        });
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Play") && !W.modified) { auto lv = load_voicebank(W.dir, false); if (lv) { Project p = test_project(*lv, W.test_lyric); std::map<std::string, Voicebank> m = a.banks; m[lv->manifest.id] = *lv; a.player->update_voicebanks(std::move(m)); a.player->preview_async(p, 0.0, p.duration_sec() + 0.8); } }
                ImGui::EndDisabled();
                if (W.rendering) { ImGui::SameLine(); ImGui::TextDisabled("rendering..."); }
                if (!W.render_error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", W.render_error.c_str());
                draw_wave(110);
                if (!W.wave.empty()) ImGui::TextDisabled("%.2f s", W.wave_sec);

                ImGui::SeparatorText("Deep validation");
                ImGui::TextWrapped("Synthesises every standard kana unit and the vowels at the range limits through the engine and checks the output is finite, audible and not clipping.");
                ImGui::BeginDisabled(W.checking || W.modified);
                if (ImGui::Button("Run completeness check")) {
                    auto lv = load_voicebank(W.dir, false);
                    if (lv) { W.checking = true; Voicebank vb = *lv; W.check_job = std::async(std::launch::async, [vb] { return check_completeness(vb); }); }
                }
                ImGui::EndDisabled();
                if (W.checking) { ImGui::SameLine(); ImGui::TextDisabled("checking..."); }
                if (W.have_report) {
                    ImGui::Text("%zu / %zu units passed", W.report.passed, W.report.total);
                    for (auto& d : W.report.diagnostics) ImGui::TextColored(d.severity == Severity::Error ? ImVec4(1, 0.4f, 0.35f, 1) : ImVec4(1, 0.8f, 0.3f, 1), "%s", d.message.c_str());
                    if (ImGui::TreeNode("Failed units")) { for (auto& c : W.report.checks) if (!c.ok) ImGui::Text("%s  {%s}: %s", c.unit.c_str(), c.phonemes.c_str(), c.problem.c_str()); ImGui::TreePop(); }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}
