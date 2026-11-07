// Menu bar, transport bar, global shortcuts, theme and font loading.
#include "app.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdlib>

namespace melovio::ui {
    bool load_japanese_font(float size) {
        ImGuiIO& io = ImGui::GetIO();
        std::vector<std::string> cands;
        if (const char* e = std::getenv("MELOVIO_FONT")) cands.push_back(e);
        for (const char* c : {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
            "/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf", "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf", "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf",
            "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf", "/usr/share/fonts/truetype/vlgothic/VL-PGothic-Regular.ttf", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            "C:\\Windows\\Fonts\\YuGothM.ttc", "C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\msgothic.ttc",
            "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc", "/Library/Fonts/Arial Unicode.ttf" }) cands.push_back(c);
        for (auto& c : cands) {
            std::error_code ec; if (!fs::exists(path_from_utf8(c), ec)) continue;
            ImFont* f = io.Fonts->AddFontFromFileTTF(c.c_str(), size, nullptr, io.Fonts->GetGlyphRangesJapanese());
            if (f) return true;
        }
        // Latin-only fallback so the UI is still crisp; Japanese lyrics will show as boxes until a Japanese font is installed.
        for (const char* c : { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", "/System/Library/Fonts/Helvetica.ttc" }) {
            std::error_code ec; if (!fs::exists(path_from_utf8(c), ec)) continue;
            if (io.Fonts->AddFontFromFileTTF(c, size)) return false;
        }
        ImFontConfig cfg; cfg.SizePixels = size; io.Fonts->AddFontDefault(&cfg);
        return false;
    }

    void apply_theme(float scale) {
        ImGuiStyle& s = ImGui::GetStyle(); s = ImGuiStyle();
        ImGui::StyleColorsDark();
        s.WindowRounding = 4; s.FrameRounding = 3; s.GrabRounding = 3; s.TabRounding = 3; s.PopupRounding = 4; s.ScrollbarRounding = 4; s.ChildRounding = 3;
        s.WindowPadding = ImVec2(8, 8); s.FramePadding = ImVec2(6, 3); s.ItemSpacing = ImVec2(7, 5); s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.TabBarBorderSize = 1;
        ImVec4* c = s.Colors;
        const ImVec4 bg(0.095f, 0.10f, 0.12f, 1), panel(0.125f, 0.13f, 0.155f, 1), raised(0.17f, 0.18f, 0.21f, 1), hover(0.23f, 0.25f, 0.30f, 1), accent(0.30f, 0.55f, 0.95f, 1);
        c[ImGuiCol_WindowBg] = bg; c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.115f, 0.14f, 1); c[ImGuiCol_PopupBg] = panel; c[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.27f, 1);
        c[ImGuiCol_FrameBg] = raised; c[ImGuiCol_FrameBgHovered] = hover; c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.36f, 1);
        c[ImGuiCol_TitleBg] = panel; c[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.16f, 0.20f, 1); c[ImGuiCol_MenuBarBg] = panel;
        c[ImGuiCol_Button] = raised; c[ImGuiCol_ButtonHovered] = hover; c[ImGuiCol_ButtonActive] = accent;
        c[ImGuiCol_Header] = ImVec4(0.22f, 0.30f, 0.46f, 0.75f); c[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.38f, 0.58f, 0.85f); c[ImGuiCol_HeaderActive] = accent;
        c[ImGuiCol_Tab] = panel; c[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.38f, 0.58f, 1); c[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.28f, 0.44f, 1);
        c[ImGuiCol_SliderGrab] = ImVec4(0.38f, 0.62f, 0.98f, 1); c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.75f, 1.f, 1); c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.78f, 1.f, 1);
        c[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.27f, 1); c[ImGuiCol_ResizeGrip] = ImVec4(0.3f, 0.35f, 0.45f, 0.4f);
        c[ImGuiCol_DockingPreview] = ImVec4(0.3f, 0.55f, 0.95f, 0.5f); c[ImGuiCol_TextSelectedBg] = ImVec4(0.3f, 0.55f, 0.95f, 0.35f);
        c[ImGuiCol_PlotHistogram] = accent;
        s.ScaleAllSizes(scale);
    }

    void handle_global_shortcuts(App& a) {
        ImGuiIO& io = ImGui::GetIO();
        if (a.file_dialog.open || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        auto chord = [](ImGuiKeyChord k) { return ImGui::IsKeyChordPressed(k); };
        if (chord(ImGuiMod_Ctrl | ImGuiKey_N)) a.request_close_project([&a] { a.new_project(); });
        if (chord(ImGuiMod_Ctrl | ImGuiKey_O)) a.request_close_project([&a] { a.file_dialog.show("open", "Open project", FileMode::Open, { kProjectExtension }, a.cfg.last_project_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_project_dir), "", [&a](const fs::path& p) { a.load_project_file(p); }); });
        if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) a.save_project_as(); else if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) a.save_project_now();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_E)) a.open_export = true;
        if (io.WantTextInput) return;                     // typing in a text box: leave plain keys alone
        if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || chord(ImGuiMod_Ctrl | ImGuiKey_Y)) a.redo(); else if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) a.undo();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_C)) a.copy_selection(false);
        if (chord(ImGuiMod_Ctrl | ImGuiKey_X)) a.copy_selection(true);
        if (chord(ImGuiMod_Ctrl | ImGuiKey_V)) a.paste_at(a.cursor_tick);
        if (chord(ImGuiMod_Ctrl | ImGuiKey_A)) a.select_all();
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) a.toggle_play();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) a.delete_selection();
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) { a.cursor_tick = 0; if (a.player) a.player->transport().seek(0); a.view.scroll_tick = 0; }
        const Tick g = grid_ticks(a.grid, a.project.tempo_map.ppq());
        if (!io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) a.nudge_selection(-g);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) a.nudge_selection(g);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) a.transpose_selection(io.KeyShift ? 12 : 1);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) a.transpose_selection(io.KeyShift ? -12 : -1);
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) a.tool = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) a.tool = Tool::Draw;
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) a.quantize_selection();
            if (ImGui::IsKeyPressed(ImGuiKey_L, false)) { a.edit("Toggle loop"); a.project.loop.enabled = !a.project.loop.enabled; a.sync_loop(); }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) a.preview_selection();
        }
    }

    void draw_main_menu(App& a) {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New project", "Ctrl+N")) a.request_close_project([&a] { a.new_project(); });
            if (ImGui::MenuItem("Open...", "Ctrl+O")) a.request_close_project([&a] { a.file_dialog.show("open", "Open project", FileMode::Open, { kProjectExtension }, a.cfg.last_project_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_project_dir), "", [&a](const fs::path& p) { a.load_project_file(p); }); });
            if (ImGui::BeginMenu("Open recent")) {
                auto rec = a.cfg.recent_existing();
                if (rec.empty()) ImGui::TextDisabled("(none)");
                for (auto& r : rec) if (ImGui::MenuItem(path_to_utf8(path_from_utf8(r).filename()).c_str(), nullptr, false, true)) { const fs::path p = path_from_utf8(r); a.open_project(p); }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) a.save_project_now();
            if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) a.save_project_as();
            ImGui::Separator();
            if (ImGui::MenuItem("Project settings...")) a.open_settings = true;
            if (ImGui::MenuItem("Export audio...", "Ctrl+E")) a.open_export = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Import MIDI...")) begin_midi_import(a);
            if (ImGui::MenuItem("Export MIDI...")) begin_midi_export(a);
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) a.request_close_project([&a] { a.quit_requested = true; });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, a.history.can_undo())) a.undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, a.history.can_redo())) a.redo();
            ImGui::Separator();
            const bool has = !a.selection.empty();
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, has)) a.copy_selection(true);
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, has)) a.copy_selection(false);
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !a.clipboard.empty())) a.paste_at(a.cursor_tick);
            if (ImGui::MenuItem("Delete", "Del", false, has)) a.delete_selection();
            if (ImGui::MenuItem("Select all", "Ctrl+A")) a.select_all();
            ImGui::Separator();
            if (ImGui::MenuItem("Transpose +1", "Up", false, has)) a.transpose_selection(1);
            if (ImGui::MenuItem("Transpose -1", "Down", false, has)) a.transpose_selection(-1);
            if (ImGui::MenuItem("Transpose +12", "Shift+Up", false, has)) a.transpose_selection(12);
            if (ImGui::MenuItem("Transpose -12", "Shift+Down", false, has)) a.transpose_selection(-12);
            if (ImGui::MenuItem("Quantize to grid", "Q", false, has)) a.quantize_selection();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Piano roll", nullptr, &a.show_roll); ImGui::MenuItem("Parameter lanes", nullptr, &a.show_params); ImGui::MenuItem("Tracks", nullptr, &a.show_tracks);
            ImGui::MenuItem("Note properties", nullptr, &a.show_props); ImGui::MenuItem("Phonemes & lyrics", nullptr, &a.show_phonemes);
            ImGui::MenuItem("Voicebanks", nullptr, &a.show_voicebanks); ImGui::MenuItem("Voicebank workshop", nullptr, &a.show_workshop);
            ImGui::MenuItem("Log & diagnostics", nullptr, &a.show_log);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) { ImGui::ClearIniSettings(); a.cfg.layout_ini.clear(); a.toast("Layout reset: restart Melovio Studio to apply"); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Melovio")) a.open_about = true;
            ImGui::MenuItem("ImGui demo (developer)", nullptr, &a.show_demo);
            ImGui::EndMenu();
        }
        // status on the right
        char st[160]; const int errs = static_cast<int>(std::count_if(a.diagnostics.begin(), a.diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; }));
        const int warns = static_cast<int>(std::count_if(a.diagnostics.begin(), a.diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
        std::snprintf(st, sizeof st, "%s%s   %d error(s)  %d warning(s)", a.dirty ? "* " : "", a.project_path.empty() ? "untitled" : path_to_utf8(a.project_path.filename()).c_str(), errs, warns);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(st).x - 16);
        if (ImGui::Selectable(st, false, 0, ImGui::CalcTextSize(st))) a.show_log = true;
        ImGui::EndMainMenuBar();
    }

    void draw_transport_bar(App& a) {
        ImGui::SetNextWindowSize(ImVec2(900, 70), ImGuiCond_FirstUseEver);
        ImGui::Begin("Transport", nullptr, ImGuiWindowFlags_NoScrollbar);
        const bool playing = a.playing();
        const bool rendering = a.player && a.player->status() == PlayerStatus::Rendering;
        if (ImGui::Button(playing ? "  Pause  " : "  Play  ")) a.toggle_play();
        ImGui::SameLine(); if (ImGui::Button("Stop")) a.stop();
        ImGui::SameLine(); if (ImGui::Button("|<")) { a.stop(); a.cursor_tick = 0; a.view.scroll_tick = 0; }
        ImGui::SameLine(); if (ImGui::Button("Preview sel.")) a.preview_selection();
        ImGui::SameLine(); bool lp = a.project.loop.enabled;
        if (ImGui::Checkbox("Loop", &lp)) { a.edit("Toggle loop"); a.project.loop.enabled = lp; if (lp && a.project.loop.end <= a.project.loop.start) { a.project.loop.start = 0; a.project.loop.end = a.project.tempo_map.bar_start_tick(4); } a.sync_loop(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag in the strip above the ruler to set the loop region.");
        const Tick ph = a.playhead_tick();
        ImGui::SameLine(0, 18); ImGui::Text("%s", tick_label(a.project.tempo_map, ph).c_str());
        ImGui::SameLine(); ImGui::TextDisabled("%.2f s", a.sec(ph));
        ImGui::SameLine(0, 18);
        float bpm = static_cast<float>(a.project.tempo_map.bpm_at(ph)); ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("BPM", &bpm, 0.5f, 20.f, 400.f, "%.1f")) {
            a.edit("Tempo", "tempo"); auto tempi = a.project.tempo_map.tempi();
            size_t idx = 0; for (size_t i = 0; i < tempi.size(); ++i) if (tempi[i].tick <= ph) idx = i;
            tempi[idx].bpm = bpm; a.project.tempo_map.set_tempi(tempi);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
        const TimeSigChange ts = a.project.tempo_map.signature_at_bar(a.project.tempo_map.bar_at_tick(ph));
        ImGui::SameLine(); ImGui::Text("%d/%d", ts.numerator, ts.denominator);
        ImGui::SameLine(0, 18);
        if (rendering) { ImGui::ProgressBar(static_cast<float>(a.player->render_progress()), ImVec2(120, 0), "rendering"); ImGui::SameLine(); if (ImGui::SmallButton("cancel")) a.player->cancel_render(); }
        else if (a.player && a.player->status() == PlayerStatus::Failed) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Render failed: %s", a.player->last_error().c_str());
        else if (!a.audio_error.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No audio device");
        else ImGui::TextDisabled(a.player && a.player->dirty() ? "changes pending render" : "ready");
        if (!a.jp_font_loaded) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "(no Japanese font found: set MELOVIO_FONT)"); }
        ImGui::End();
    }
}
