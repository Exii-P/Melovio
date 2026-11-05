// Parameter lanes: automation curves for the expression channels the Wauvio-Vocal engine really supports (see core/params.hpp).
#include "app.hpp"
#include <algorithm>
#include <cmath>

namespace melovio::ui {
    namespace {
        struct LaneDrag { int index = -1; bool active = false; std::string param; };
        LaneDrag g_drag;
    }

    void draw_param_lanes(App& a) {
        if (!a.show_params) return;
        ImGui::SetNextWindowSize(ImVec2(900, 200), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Parameter Lanes", &a.show_params)) { ImGui::End(); return; }
        Track* t = a.cur_track();
        if (!t) { ImGui::TextDisabled("No track."); ImGui::End(); return; }

        // lane selector: only parameters the engine supports as expression channels
        ImGui::SetNextItemWidth(190);
        const ParamDef* cur = find_param(a.lane_param);
        if (!cur || !cur->lane) { a.lane_param = "pitch"; cur = find_param("pitch"); }
        if (ImGui::BeginCombo("Parameter", cur->label)) {
            for (auto& p : param_registry()) {
                if (!p.lane) continue;
                const Lane* l = t->find_lane(p.id);
                std::string label = std::string(p.label) + (l && !l->points.empty() ? "  *" : "");
                if (ImGui::Selectable(label.c_str(), a.lane_param == p.id)) a.lane_param = p.id;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::TextDisabled("%s", cur->tip);
        ImGui::SameLine();
        Lane* lane = t->find_lane(a.lane_param);
        if (ImGui::SmallButton("Clear lane") && lane && !lane->points.empty()) {
            a.edit("Clear lane");
            t = a.cur_track(); t->lanes.erase(std::remove_if(t->lanes.begin(), t->lanes.end(), [&](const Lane& l) { return l.param == a.lane_param; }), t->lanes.end());
            lane = nullptr;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click empty space to add a point, drag to move, right-click a point to delete it.");

        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail(); avail.y = std::max(avail.y, 60.f);
        const float kw = a.view.keys_w;
        const ImVec2 plot0(p0.x + kw, p0.y), plot1(p0.x + avail.x, p0.y + avail.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int ppq = a.project.tempo_map.ppq();
        const double ppt = a.view.px_per_tick(ppq);
        auto x_of = [&](double tick) { return plot0.x + static_cast<float>((tick - a.view.scroll_tick) * ppt); };
        auto t_of = [&](float x) { return static_cast<Tick>(std::llround((x - plot0.x) / ppt + a.view.scroll_tick)); };
        const float lo = cur->min, hi = cur->max;
        auto y_of = [&](float v) { return plot1.y - 4 - (std::clamp(v, lo, hi) - lo) / (hi - lo) * (avail.y - 8); };
        auto v_of = [&](float y) { return std::clamp(lo + (plot1.y - 4 - y) / (avail.y - 8) * (hi - lo), lo, hi); };

        ImGui::InvisibleButton("##lane", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        dl->PushClipRect(p0, plot1, true);
        dl->AddRectFilled(p0, plot1, IM_COL32(24, 26, 32, 255));
        dl->AddRectFilled(p0, ImVec2(plot0.x, plot1.y), IM_COL32(32, 35, 42, 255));
        // beat / bar grid in sync with the piano roll
        const TempoMap& tm = a.project.tempo_map;
        const Tick beat = tm.ticks_per_beat(tm.signature_at_bar(0).denominator);
        const Tick first = std::max<Tick>(0, static_cast<Tick>(a.view.scroll_tick) / beat * beat);
        for (Tick k = first; x_of(static_cast<double>(k)) < plot1.x; k += beat) {
            const float x = x_of(static_cast<double>(k)); if (x < plot0.x) continue;
            const bool barline = tm.bar_beat(k).beat == 0 && tm.bar_beat(k).tick_in_beat == 0;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, plot1.y), barline ? IM_COL32(70, 74, 86, 255) : IM_COL32(42, 45, 54, 255));
        }
        // default / zero line
        const float yz = y_of(cur->def);
        dl->AddLine(ImVec2(plot0.x, yz), ImVec2(plot1.x, yz), IM_COL32(110, 115, 130, 160), 1.f);
        // note spans for orientation
        for (auto& n : t->notes) {
            const float xa = x_of(static_cast<double>(n.tick)), xb = x_of(static_cast<double>(n.end()));
            if (xb < plot0.x || xa > plot1.x) continue;
            dl->AddRectFilled(ImVec2(std::max(xa, plot0.x), plot1.y - 4), ImVec2(std::max(xb - 1, plot0.x), plot1.y), track_color(a.track_index, 0.7f));
        }
        // curve
        const ImU32 col = IM_COL32(255, 200, 90, 255);
        ImVec2 mouse = ImGui::GetIO().MousePos;
        int hover_pt = -1;
        if (lane && !lane->points.empty()) {
            auto& pts = lane->points;
            ImVec2 prev(plot0.x, y_of(pts.front().value));
            for (size_t i = 0; i < pts.size(); ++i) {
                ImVec2 cp(x_of(static_cast<double>(pts[i].tick)), y_of(pts[i].value));
                dl->AddLine(prev, cp, col, 1.8f); prev = cp;
                if (std::hypot(mouse.x - cp.x, mouse.y - cp.y) < 7.f) hover_pt = static_cast<int>(i);
            }
            dl->AddLine(prev, ImVec2(plot1.x, prev.y), col, 1.8f);
            for (size_t i = 0; i < pts.size(); ++i) {
                ImVec2 cp(x_of(static_cast<double>(pts[i].tick)), y_of(pts[i].value));
                dl->AddCircleFilled(cp, static_cast<int>(i) == hover_pt || (g_drag.active && g_drag.index == static_cast<int>(i)) ? 5.5f : 3.8f, col);
            }
        }
        // axis labels
        char b[48];
        std::snprintf(b, sizeof b, "%g%s", hi, cur->unit); dl->AddText(ImVec2(p0.x + 4, p0.y + 2), IM_COL32(170, 175, 190, 255), b);
        std::snprintf(b, sizeof b, "%g%s", lo, cur->unit); dl->AddText(ImVec2(p0.x + 4, plot1.y - 16), IM_COL32(170, 175, 190, 255), b);
        // playhead
        const float xp = x_of(static_cast<double>(a.playhead_tick()));
        if (xp >= plot0.x && xp <= plot1.x) dl->AddLine(ImVec2(xp, p0.y), ImVec2(xp, plot1.y), IM_COL32(255, 90, 90, 255), 1.5f);
        dl->PopClipRect();

        ImGuiIO& io = ImGui::GetIO();
        const bool in_plot = mouse.x >= plot0.x;
        if (hovered && in_plot) {
            if (hover_pt >= 0 && lane) {
                const auto& pt = lane->points[static_cast<size_t>(hover_pt)];
                ImGui::SetTooltip("%s @ %s\n%.3f %s", cur->label, tick_label(tm, pt.tick).c_str(), static_cast<double>(pt.value), cur->unit);
            } else ImGui::SetTooltip("%.3f %s", static_cast<double>(v_of(mouse.y)), cur->unit);
            if (io.MouseWheel != 0 && io.KeyCtrl) {
                const double tick_under = static_cast<double>(t_of(mouse.x));
                a.view.px_per_quarter = std::clamp(a.view.px_per_quarter * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 20.f, 400.f);
                a.view.scroll_tick = std::max(0.0, tick_under - (mouse.x - plot0.x) / a.view.px_per_tick(ppq));
            } else if (io.MouseWheel != 0) a.view.scroll_tick = std::max(0.0, a.view.scroll_tick - io.MouseWheel * 80 / ppt * 0.25);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                a.edit("Edit lane " + std::string(cur->label));
                t = a.cur_track(); lane = &t->ensure_lane(a.lane_param);
                if (hover_pt >= 0) { g_drag = { hover_pt, true, a.lane_param }; }
                else {
                    lane->set_point(a.snap(t_of(mouse.x)), v_of(mouse.y));
                    // find the index of the point we just added so it can be dragged immediately
                    Tick tk = a.snap(t_of(mouse.x)); int idx = -1;
                    for (size_t i = 0; i < lane->points.size(); ++i) if (lane->points[i].tick == tk) idx = static_cast<int>(i);
                    g_drag = { idx, true, a.lane_param };
                }
                a.touch();
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hover_pt >= 0 && lane) {
                a.edit("Delete lane point");
                t = a.cur_track(); lane = t->find_lane(a.lane_param);
                if (lane && hover_pt < static_cast<int>(lane->points.size())) lane->points.erase(lane->points.begin() + hover_pt);
                if (lane && lane->points.empty()) t->lanes.erase(std::remove_if(t->lanes.begin(), t->lanes.end(), [&](const Lane& l) { return l.param == a.lane_param; }), t->lanes.end());
            }
        }
        if (g_drag.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                lane = t->find_lane(g_drag.param);
                if (lane && g_drag.index >= 0 && g_drag.index < static_cast<int>(lane->points.size())) {
                    auto& pts = lane->points; auto& pt = pts[static_cast<size_t>(g_drag.index)];
                    Tick nt = std::max<Tick>(0, a.snap(t_of(mouse.x)));
                    const Tick lo_t = g_drag.index > 0 ? pts[static_cast<size_t>(g_drag.index) - 1].tick + 1 : 0;
                    const Tick hi_t = g_drag.index + 1 < static_cast<int>(pts.size()) ? pts[static_cast<size_t>(g_drag.index) + 1].tick - 1 : INT64_MAX;
                    pt.tick = std::clamp(nt, lo_t, hi_t);
                    pt.value = v_of(mouse.y);
                    if (io.KeyShift) pt.value = cur->def;
                    a.touch();
                }
            } else g_drag.active = false;
        }
        ImGui::End();
    }
}
