// Note properties: the selected note(s) numeric values and per-note engine parameter overrides.
#include "app.hpp"
#include <algorithm>

namespace melovio::ui {
    void draw_properties(App& a) {
        if (!a.show_props) return;
        ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Note Properties", &a.show_props)) { ImGui::End(); return; }
        Track* t = a.cur_track();
        auto sel = a.selected_notes();
        if (!t || sel.empty()) { ImGui::TextDisabled("Select a note in the piano roll."); ImGui::End(); return; }
        Note* first = sel.front();
        if (sel.size() > 1) ImGui::Text("%zu notes selected (edits apply to all; values shown are from the first)", sel.size());
        const TempoMap& tm = a.project.tempo_map;
        ImGui::Text("Start %s   End %s", tick_label(tm, first->tick).c_str(), tick_label(tm, first->end()).c_str());
        ImGui::Text("%.3f s - %.3f s", a.sec(first->tick), a.sec(first->end()));

        // lyric (single note only; bulk assignment lives in the Phonemes panel)
        if (sel.size() == 1) {
            char buf[256]; std::snprintf(buf, sizeof buf, "%s", first->lyric.c_str());
            if (ImGui::InputText("Lyric", buf, sizeof buf)) { const NoteId id = first->id; a.edit("Edit lyric", "lyric:" + std::to_string(id)); if (Note* n = a.cur_track()->find_note(id)) { n->lyric = buf; n->phonemes.clear(); } }
            if (ImGui::IsItemDeactivated()) a.history.break_coalescing();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kana, romaji, or {phoneme notation}. Editing the lyric clears a manual pronunciation override.");
        }
        int pitch = first->pitch;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Pitch (MIDI)", &pitch)) { pitch = std::clamp(pitch, 0, 127); const int d = pitch - first->pitch; a.edit("Pitch"); for (auto* n : a.selected_notes()) n->pitch = std::clamp(n->pitch + d, 0, 127); }
        ImGui::SameLine(); ImGui::TextDisabled("%s", pitch_name(first->pitch));
        if (const Voicebank* vb = a.vb_of(*t); vb && vb->profile_ok) {
            auto f = a.note_flags.find(first->id);
            if (f != a.note_flags.end() && f->second & 4) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Outside the voice's hard range (%d..%d)", vb->profile.hard_low, vb->profile.hard_high);
            else if (f != a.note_flags.end() && f->second & 2) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Outside the comfortable range (%d..%d)", vb->profile.range_low, vb->profile.range_high);
        }
        int len = static_cast<int>(first->length);
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Length (ticks)", &len, static_cast<int>(grid_ticks(a.grid, tm.ppq())))) { len = std::max(1, len); const Tick d = len - first->length; a.edit("Length"); for (auto* n : a.selected_notes()) n->length = std::max<Tick>(1, n->length + d); a.cur_track()->resolve_overlaps(a.selection); }
        int start = static_cast<int>(first->tick);
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Start (ticks)", &start, static_cast<int>(grid_ticks(a.grid, tm.ppq())))) { start = std::max(0, start); const Tick d = start - first->tick; a.edit("Start"); for (auto* n : a.selected_notes()) n->tick = std::max<Tick>(0, n->tick + d); a.cur_track()->resolve_overlaps(a.selection); }
        float vel = first->velocity;
        if (ImGui::SliderFloat("Velocity", &vel, 0.f, 1.f, "%.2f")) { a.edit("Velocity", "vel"); for (auto* n : a.selected_notes()) n->velocity = vel; }
        if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
        float off = static_cast<float>(first->offset_ms);
        if (ImGui::SliderFloat("Timing offset (ms)", &off, -200.f, 200.f, "%.0f")) { a.edit("Timing offset", "off"); for (auto* n : a.selected_notes()) n->offset_ms = off; }
        if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shifts the sound without moving the note on the grid (negative = earlier).");

        ImGui::SeparatorText("Voice parameters for this note");
        ImGui::TextDisabled("Unchecked = inherit the track / voicebank value.");
        std::string group;
        for (auto& pd : param_registry()) {
            if (!pd.note_param) continue;
            if (group != pd.group) { group = pd.group; ImGui::TextDisabled("%s", pd.group); }
            ImGui::PushID(pd.id);
            auto it = first->params.find(pd.id); bool set = it != first->params.end();
            if (ImGui::Checkbox("##set", &set)) { a.edit("Note parameter"); for (auto* n : a.selected_notes()) { if (set) n->params[pd.id] = pd.def; else n->params.erase(pd.id); } first = a.selected_notes().front(); it = first->params.find(pd.id); }
            ImGui::SameLine();
            ImGui::BeginDisabled(!set);
            float v = set && it != first->params.end() ? it->second : pd.def;
            const std::string fmt = std::string("%.2f") + (pd.unit[0] ? std::string(" ") + pd.unit : "");
            ImGui::SetNextItemWidth(-120);
            if (ImGui::SliderFloat(pd.label, &v, pd.min, pd.max, fmt.c_str())) { a.edit("Note parameter", std::string("np:") + pd.id); for (auto* n : a.selected_notes()) n->params[pd.id] = v; }
            if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", pd.tip);
            ImGui::PopID();
            if (a.selected_notes().empty()) break;
            first = a.selected_notes().front();
        }
        ImGui::End();
    }
}
