// Pronunciation editor: lyric -> syllables -> phonemes, with manual override, timing weights and bulk lyric entry.
#include "app.hpp"
#include <algorithm>
#include <sstream>

namespace melovio::ui {
    namespace {
        char g_override[256];
        NoteId g_override_for = 0;
        std::string g_override_err;
        char g_bulk[16384];
        int g_sheet_mode = 0;               // 0 = word per note, 1 = kana (mora) per note
        bool g_sheet_skip_headers = true;

        ImU32 kind_color(Phoneme::Kind k) {
            switch (k) { case Phoneme::Kind::Consonant: return IM_COL32(230, 150, 80, 255); case Phoneme::Kind::Vowel: return IM_COL32(90, 160, 235, 255); default: return IM_COL32(170, 120, 220, 255); }
        }

        void apply_override(App& a, NoteId id, const std::string& text) {
            a.edit("Pronunciation override", "ovr:" + std::to_string(id));
            if (Note* n = a.cur_track()->find_note(id)) n->phonemes = text;
            a.analysis_cache.clear();
        }

        // Replaces the lyrics of consecutive notes with whitespace separated words (one word per note).
        void distribute_lyrics(App& a, const std::string& text, bool from_first_selected) {
            Track* t = a.cur_track(); if (!t) return;
            std::vector<std::string> words = parse_lyric_sheet(text, g_sheet_mode == 0 ? LyricSheetMode::Words : LyricSheetMode::Morae, g_sheet_skip_headers);
            if (words.empty()) { a.toast("No lyrics found in the text", Severity::Warning); return; }
            size_t start = 0;
            if (from_first_selected) { start = t->notes.size(); for (size_t i = 0; i < t->notes.size(); ++i) if (a.selection.count(t->notes[i].id)) { start = i; break; } }
            if (start >= t->notes.size()) { a.toast("Select the first note to receive lyrics", Severity::Warning); return; }
            a.edit("Distribute lyrics"); t = a.cur_track();
            size_t k = 0;
            for (size_t i = start; i < t->notes.size() && k < words.size(); ++i, ++k) { t->notes[i].lyric = words[k]; t->notes[i].phonemes.clear(); }
            a.analysis_cache.clear();
            a.toast("Assigned " + std::to_string(k) + " lyric(s)" + (k < words.size() ? " - " + std::to_string(words.size() - k) + " left over (not enough notes)" : ""));
        }
    }

    void draw_phonemes(App& a) {
        if (!a.show_phonemes) return;
        ImGui::SetNextWindowSize(ImVec2(560, 440), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Phonemes & Lyrics", &a.show_phonemes)) { ImGui::End(); return; }
        Track* t = a.cur_track();
        if (!t) { ImGui::End(); return; }
        const Voicebank* vb = a.vb_of(*t);
        auto sel = a.selected_notes();

        if (ImGui::BeginTabBar("##ph")) {
            if (ImGui::BeginTabItem("Selected note")) {
                if (sel.empty()) ImGui::TextDisabled("Select a note to inspect and edit its pronunciation.");
                else {
                    Note& n = *sel.front();
                    const NoteId id = n.id;
                    const LyricAnalysis& an = a.analysis_of(n, vb);
                    ImGui::Text("Lyric:  %s", n.lyric.c_str());
                    ImGui::SameLine(); ImGui::TextDisabled("  note %s, %.3f s", pitch_name(n.pitch), a.sec(n.end()) - a.sec(n.tick));
                    if (an.from_override) ImGui::TextColored(ImVec4(0.7f, 0.55f, 0.95f, 1), "Pronunciation is %s", n.phonemes.empty() ? "from the voicebank's phonetic mapping" : "a manual override");
                    // syllable / phoneme strip, widths proportional to the time share each syllable gets
                    float total = 0; for (auto& s : an.syllables) total += s.weight;
                    const float w = std::max(120.f, ImGui::GetContentRegionAvail().x - 4), h = 52.f;
                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(w, h + 18));
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float x = p0.x;
                    for (size_t i = 0; i < an.syllables.size(); ++i) {
                        const Syllable& s = an.syllables[i];
                        const float sw = total > 0 ? w * s.weight / total : w;
                        dl->AddRectFilled(ImVec2(x + 1, p0.y), ImVec2(x + sw - 1, p0.y + h), IM_COL32(38, 42, 52, 255), 4.f);
                        dl->AddRect(ImVec2(x + 1, p0.y), ImVec2(x + sw - 1, p0.y + h), s.representable ? IM_COL32(80, 86, 100, 255) : IM_COL32(230, 80, 70, 255), 4.f);
                        dl->AddText(ImVec2(x + 5, p0.y + 3), IM_COL32(200, 205, 220, 255), s.source.c_str());
                        float px = x + 5;
                        for (auto& ph : s.phonemes) {
                            const ImVec2 ts = ImGui::CalcTextSize(ph.symbol.c_str());
                            dl->AddRectFilled(ImVec2(px, p0.y + 26), ImVec2(px + ts.x + 8, p0.y + 46), kind_color(ph.kind), 3.f);
                            dl->AddText(ImVec2(px + 4, p0.y + 28), IM_COL32(15, 15, 20, 255), ph.symbol.c_str());
                            px += ts.x + 11;
                        }
                        x += sw;
                    }
                    if (an.syllables.empty()) dl->AddText(p0, IM_COL32(150, 150, 160, 255), "(no sound)");
                    ImGui::TextDisabled("Block widths show each syllable's share of the note's duration.");
                    ImGui::Text("Phonemes: %s", an.phoneme_text.empty() ? "-" : an.phoneme_text.c_str());
                    for (auto& d : an.diagnostics) {
                        const ImVec4 c = d.severity == Severity::Error ? ImVec4(1, 0.4f, 0.35f, 1) : d.severity == Severity::Warning ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.7f, 0.75f, 0.85f, 1);
                        ImGui::TextColored(c, "%s", d.message.c_str());
                    }
                    // particle helper (は へ を)
                    const std::string pp = particle_pronunciation(n.lyric);
                    if (!pp.empty() && n.phonemes.empty()) {
                        ImGui::Text("\"%s\" can be a particle. Sung as written it is read as ", n.lyric.c_str()); ImGui::SameLine();
                        if (ImGui::SmallButton(("Use particle reading {" + pp + "}").c_str())) apply_override(a, id, "{" + pp + "}");
                    }
                    ImGui::SeparatorText("Manual pronunciation (phoneme notation)");
                    if (g_override_for != id) { std::snprintf(g_override, sizeof g_override, "%s", n.phonemes.c_str()); g_override_for = id; g_override_err.clear(); }
                    ImGui::SetNextItemWidth(-190);
                    const bool entered = ImGui::InputTextWithHint("##ovr", "e.g. {k a}  /  {sh i N}  (empty = from lyric)", g_override, sizeof g_override, ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine(); const bool apply = ImGui::Button("Apply"); ImGui::SameLine();
                    if (ImGui::Button("Use analysed") && !an.phoneme_text.empty()) { std::snprintf(g_override, sizeof g_override, "%s", an.phoneme_text.c_str()); }
                    ImGui::SameLine(); if (ImGui::Button("Clear")) { g_override[0] = 0; apply_override(a, id, ""); g_override_err.clear(); }
                    if (entered || apply) {
                        if (!g_override[0]) { apply_override(a, id, ""); g_override_err.clear(); }
                        else { g_override_err = validate_phoneme_text(g_override); if (g_override_err.empty()) { std::string s = g_override; if (s.front() != '{') s = "{" + s + "}"; apply_override(a, id, s); } }
                    }
                    if (!g_override_err.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Not applied: %s", g_override_err.c_str());
                    ImGui::TextDisabled("Symbols: vowels a i u e o; consonants k s sh t ch n h f m y r w g z j d b p ...; N moraic nasal; Q small tsu; - long vowel.");

                    // timing: per-syllable weight / consonant length (rewrites the pronunciation as an explicit override)
                    if (an.syllables.size() > 0 && std::all_of(an.syllables.begin(), an.syllables.end(), [](const Syllable& s) { return s.representable; })) {
                        ImGui::SeparatorText("Syllable timing");
                        std::vector<Syllable> syl = an.syllables; bool changed = false;
                        for (size_t i = 0; i < syl.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::Text("%s", syl[i].source.c_str()); ImGui::SameLine(70);
                            ImGui::SetNextItemWidth(120); changed |= ImGui::SliderFloat("##w", &syl[i].weight, 0.2f, 4.f, "length x%.2f");
                            ImGui::SameLine(); ImGui::SetNextItemWidth(120); changed |= ImGui::SliderFloat("##c", &syl[i].consonant_scale, 0.3f, 2.5f, "consonant x%.2f");
                            ImGui::PopID();
                        }
                        if (changed) {
                            std::string text; for (size_t i = 0; i < syl.size(); ++i) { if (i) text += ' '; text += syllable_token(syl[i]); }
                            const std::string err = validate_phoneme_text(text);
                            const LyricAnalysis check = err.empty() ? a.pron.analyze_phonemes("{" + text + "}") : LyricAnalysis{};
                            if (err.empty() && check.syllables.size() == syl.size()) { apply_override(a, id, "{" + text + "}"); g_override_for = 0; }
                            else g_override_err = err.empty() ? "could not express this timing in phoneme notation" : err;
                        }
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lyric entry")) {
                ImGui::TextWrapped("Paste or load a lyric sheet. Each unit goes to one note, starting from the first selected note or the start of the track. Kana, romaji, or {phonemes} are accepted. Kanji needs a reading (see user dictionary) and is flagged otherwise.");
                ImGui::InputTextMultiline("##bulk", g_bulk, sizeof g_bulk, ImVec2(-1, 120));
                if (ImGui::Button("Load lyric sheet (.txt)...")) {
                    a.file_dialog.show("lyricsheet", "Load lyric sheet", FileMode::Open, { ".txt" }, a.cfg.last_import_dir.empty() ? fs::current_path() : path_from_utf8(a.cfg.last_import_dir), "", [&a](const fs::path& p) {
                        std::string err; const std::string txt = read_text_file(p, &err);
                        if (!err.empty()) { a.toast("Cannot read the file: " + err, Severity::Error); return; }
                        a.cfg.last_import_dir = path_to_utf8(p.parent_path());
                        if (txt.size() >= sizeof g_bulk) a.toast("The lyric sheet is longer than the text box and was cut off", Severity::Warning);
                        std::snprintf(g_bulk, sizeof g_bulk, "%s", txt.c_str());
                    });
                }
                ImGui::SameLine(); ImGui::RadioButton("One word per note", &g_sheet_mode, 0);
                ImGui::SameLine(); ImGui::RadioButton("One kana per note", &g_sheet_mode, 1);
                ImGui::Checkbox("Skip [section] headers", &g_sheet_skip_headers);
                const size_t units = parse_lyric_sheet(g_bulk, g_sheet_mode == 0 ? LyricSheetMode::Words : LyricSheetMode::Morae, g_sheet_skip_headers).size();
                ImGui::SameLine(); ImGui::TextDisabled("%zu unit(s) found, track has %zu note(s)", units, t->notes.size());
                if (ImGui::Button("Assign from first selected note")) distribute_lyrics(a, g_bulk, true);
                ImGui::SameLine(); if (ImGui::Button("Assign from track start")) distribute_lyrics(a, g_bulk, false);
                ImGui::TextDisabled("Default lyric for new notes:");
                ImGui::SameLine(); ImGui::SetNextItemWidth(80); char dl[64]; std::snprintf(dl, sizeof dl, "%s", a.default_lyric.c_str()); if (ImGui::InputText("##dl", dl, sizeof dl)) a.default_lyric = dl;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("All notes")) {
                if (ImGui::BeginTable("notes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, 0))) {
                    ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 80); ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, 44);
                    ImGui::TableSetupColumn("Lyric", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableSetupColumn("Phonemes"); ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110);
                    ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
                    for (auto& n : t->notes) {
                        const LyricAnalysis& an = a.analysis_of(n, vb);
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        const bool is_sel = a.selection.count(n.id) > 0;
                        if (ImGui::Selectable(tick_label(a.project.tempo_map, n.tick).c_str(), is_sel, ImGuiSelectableFlags_SpanAllColumns)) {
                            a.select_only(n.id); a.cursor_tick = n.tick;
                        }
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(pitch_name(n.pitch));
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(n.lyric.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(an.phoneme_text.c_str());
                        ImGui::TableNextColumn();
                        if (!an.pronounceable) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "unpronounceable");
                        else if (has_errors(an.diagnostics)) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "error");
                        else if (!n.phonemes.empty()) ImGui::TextColored(ImVec4(0.7f, 0.55f, 0.95f, 1), "override");
                        else if (!an.diagnostics.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "check");
                        else ImGui::TextDisabled("ok");
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}
