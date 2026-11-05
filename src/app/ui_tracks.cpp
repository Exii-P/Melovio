// Track list and per-track voice settings.
#include "app.hpp"
#include <algorithm>

namespace melovio::ui {
    namespace {
        // The track-wide Style overrides worth exposing directly. Everything is an engine Style field (see core/params.hpp).
        const char* kTrackStyle[] = { "brightness", "breathiness", "tension", "formant_shift", "mouth_open", "power", "softness", "vibrato_depth", "vibrato_rate", "portamento", "consonant_strength", "legato" };
    }

    void draw_tracks(App& a) {
        if (!a.show_tracks) return;
        ImGui::SetNextWindowSize(ImVec2(320, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Tracks", &a.show_tracks)) { ImGui::End(); return; }
        if (ImGui::Button("+ Add track")) {
            a.edit("Add track");
            const Voicebank* vb = a.cur_track() ? a.vb_of(*a.cur_track()) : nullptr;
            const std::string id = vb ? vb->manifest.id : (a.lib.all().empty() ? "yune" : a.lib.all().front().manifest.id);
            a.project.add_track("Vocal " + std::to_string(a.project.tracks.size() + 1), id);
            a.track_index = static_cast<int>(a.project.tracks.size()) - 1;
            a.selection.clear(); a.refresh_voicebanks();
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove") && a.project.tracks.size() > 1) ImGui::OpenPopup("rmtrack");
        if (ImGui::BeginPopup("rmtrack")) {
            ImGui::Text("Remove track \"%s\" and its notes?", a.cur_track()->name.c_str());
            if (ImGui::Button("Remove")) {
                const std::string id = a.cur_track()->id; a.edit("Remove track"); a.project.remove_track(id);
                a.track_index = std::max(0, a.track_index - 1); a.selection.clear(); a.refresh_voicebanks(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(a.project.tracks.size()); ++i) {
            Track& t = a.project.tracks[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::ColorButton("##c", ImGui::ColorConvertU32ToFloat4(track_color(i)), ImGuiColorEditFlags_NoTooltip, ImVec2(10, 18));
            ImGui::SameLine();
            if (ImGui::Selectable(t.name.c_str(), a.track_index == i, 0, ImVec2(ImGui::GetContentRegionAvail().x - 70, 0))) { a.track_index = i; a.selection.clear(); a.diag_stale = true; }
            ImGui::SameLine();
            bool m = t.mute, s = t.solo;
            ImGui::PushStyleColor(ImGuiCol_Button, m ? ImVec4(0.8f, 0.3f, 0.3f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::SmallButton("M")) { a.edit("Mute"); t.mute = !m; }
            ImGui::PopStyleColor(); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, s ? ImVec4(0.8f, 0.65f, 0.15f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::SmallButton("S")) { a.edit("Solo"); t.solo = !s; }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Separator();

        Track* t = a.cur_track();
        if (!t) { ImGui::End(); return; }
        ImGui::TextDisabled("Selected track");
        char nm[128]; std::snprintf(nm, sizeof nm, "%s", t->name.c_str());
        if (ImGui::InputText("Name", nm, sizeof nm)) { a.edit("Rename track", "trackname:" + t->id); a.cur_track()->name = nm; }
        if (ImGui::IsItemDeactivated()) a.history.break_coalescing();

        // voicebank
        const Voicebank* cur = a.vb_of(*t);
        if (ImGui::BeginCombo("Voicebank", cur ? (cur->manifest.name + " " + cur->manifest.version).c_str() : (t->voicebank_id + " (missing)").c_str())) {
            for (auto& vb : a.lib.all()) {
                if (!vb.usable()) continue;
                const bool sel = vb.manifest.id == t->voicebank_id;
                const std::string label = vb.manifest.name + " " + vb.manifest.version + (vb.manifest.native_name.empty() ? "" : "  " + vb.manifest.native_name);
                if (ImGui::Selectable(label.c_str(), sel)) {
                    a.edit("Change voicebank");
                    t = a.cur_track(); t->voicebank_id = vb.manifest.id; t->persona.clear();
                    a.project.ensure_voicebank_ref(vb.manifest.id, vb.manifest.version, vb.manifest.name);
                    a.refresh_voicebanks(); a.analysis_cache.clear();
                }
            }
            ImGui::EndCombo();
        }
        cur = a.vb_of(*t);
        if (!cur) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Voicebank \"%s\" is not installed. See Voicebanks > Locate.", t->voicebank_id.c_str());

        if (cur && !cur->profile.personas.empty()) {
            if (ImGui::BeginCombo("Persona", t->persona.empty() ? "(voicebank default)" : t->persona.c_str())) {
                if (ImGui::Selectable("(voicebank default)", t->persona.empty())) { a.edit("Persona"); a.cur_track()->persona.clear(); }
                for (auto& p : cur->profile.personas) if (ImGui::Selectable(p.c_str(), p == t->persona)) { a.edit("Persona"); a.cur_track()->persona = p; }
                ImGui::EndCombo();
            }
            if (!t->persona.empty()) { float v = t->persona_amount; if (ImGui::SliderFloat("Persona amount", &v, 0, 1, "%.2f")) { a.edit("Persona amount", "pamt"); a.cur_track()->persona_amount = v; } if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing(); }
        }

        auto slider = [&](const char* label, float* v, float lo, float hi, const char* fmt, const char* key, const char* tip = nullptr) {
            float tmp = *v;
            if (ImGui::SliderFloat(label, &tmp, lo, hi, fmt)) { a.edit(label, std::string(key) + t->id); *v = tmp; }
            if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };
        ImGui::SeparatorText("Mix");
        t = a.cur_track();
        slider("Gain (dB)", &t->gain_db, -36.f, 12.f, "%.1f", "gain");
        t = a.cur_track(); slider("Pan", &t->pan, -1.f, 1.f, "%.2f", "pan", "Balance (equal-power); mono exports sum both sides.");
        ImGui::SeparatorText("Voice character");
        t = a.cur_track();
        slider("Natural", &t->natural, 0.f, 1.f, "%.2f", "natural", "Wauvio's 'living voice' model: small pitch, timing and breath variations. 0 = off. Deterministic per take.");
        t = a.cur_track();
        if (t->natural > 0) { int take = t->natural_take; if (ImGui::InputInt("Take", &take)) { take = std::clamp(take, 1, 99); a.edit("Natural take"); a.cur_track()->natural_take = take; } t = a.cur_track(); }
        bool ae = t->auto_expression;
        if (ImGui::Checkbox("Auto expression", &ae)) { a.edit("Auto expression"); a.cur_track()->auto_expression = ae; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wauvio's phrase-level automatic dynamics and vibrato shaping.");

        if (ImGui::CollapsingHeader("Track style overrides")) {
            ImGui::TextDisabled("Unset = the voicebank's own value. Notes can override individually.");
            for (const char* key : kTrackStyle) {
                const ParamDef* pd = find_param(key); if (!pd) continue;
                t = a.cur_track();
                auto it = t->style.find(key); bool set = it != t->style.end();
                ImGui::PushID(key);
                if (ImGui::Checkbox("##set", &set)) { a.edit("Style override"); t = a.cur_track(); if (set) t->style[key] = pd->def; else t->style.erase(key); t = a.cur_track(); it = t->style.find(key); }
                ImGui::SameLine();
                ImGui::BeginDisabled(!set);
                float v = set ? it->second : pd->def;
                const std::string fmt = std::string("%.2f") + (pd->unit[0] ? std::string(" ") + pd->unit : "");
                ImGui::SetNextItemWidth(-130);
                if (ImGui::SliderFloat(pd->label, &v, pd->min, pd->max, fmt.c_str()) && set) { a.edit("Style override", std::string("ts:") + key + t->id); a.cur_track()->style[key] = v; }
                if (ImGui::IsItemDeactivatedAfterEdit()) a.history.break_coalescing();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", pd->tip);
                ImGui::PopID();
            }
        }
        ImGui::End();
    }
}
