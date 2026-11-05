// Piano roll: timeline, pitch grid, note editing, loop region, playhead.
#include "app.hpp"
#include <algorithm>
#include <cmath>

namespace melovio::ui {
    namespace {
        enum class Drag { None, Move, ResizeR, ResizeL, Rubber, Loop, Scrub, Pan };

        struct DragState {
            Drag mode = Drag::None;
            ImVec2 start{ 0, 0 }, cur{ 0, 0 };
            Track snapshot;
            std::set<NoteId> ids;
            NoteId grab = 0;
            bool recorded = false;
            Tick loop_anchor = 0;
            bool additive = false;
            float pan_scroll_pitch = 0; double pan_scroll_tick = 0;
        };
        DragState g;
        ImVec2 g_lyric_pos;
        char g_lyric_buf[256];

        bool is_black(int midi) { switch (((midi % 12) + 12) % 12) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; } }

        const GridDivision kGrids[] = { { 4, false }, { 8, false }, { 16, false }, { 32, false }, { 4, true }, { 8, true }, { 16, true } };
        const char* kGridNames[] = { "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T" };
    }

    void draw_piano_roll(App& a) {
        if (!a.show_roll) return;
        ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Piano Roll", &a.show_roll, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
        Track* t = a.cur_track();
        if (!t) { ImGui::TextDisabled("No track. Add one in the Tracks panel."); ImGui::End(); return; }
        const int ppq = a.project.tempo_map.ppq();
        const TempoMap& tm = a.project.tempo_map;
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::RadioButton("Select (V)", a.tool == Tool::Select)) a.tool = Tool::Select;
        ImGui::SameLine(); if (ImGui::RadioButton("Draw (D)", a.tool == Tool::Draw)) a.tool = Tool::Draw;
        ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        int gi = 2; for (int i = 0; i < 7; ++i) if (kGrids[i].denominator == a.grid.denominator && kGrids[i].triplet == a.grid.triplet) gi = i;
        if (ImGui::Combo("Grid", &gi, kGridNames, 7)) a.grid = kGrids[gi];
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap grid and default note length. Triplet grids divide a beat in three.");
        ImGui::SameLine(); ImGui::Checkbox("Snap", &a.snap_enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold Alt while dragging to bypass snapping temporarily.");
        ImGui::SameLine(); ImGui::Checkbox("Follow", &a.follow_playhead);
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::SliderFloat("##zx", &a.view.px_per_quarter, 20.f, 400.f, "H %.0f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::SliderFloat("##zy", &a.view.px_per_semitone, 8.f, 36.f, "V %.0f");
        ImGui::SameLine(); ImGui::TextDisabled("Track: %s", t->name.c_str());

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail(); avail.y = std::max(avail.y, 80.f);
        const float ruler_h = 20.f, loop_h = 9.f, top = ruler_h + loop_h;
        const float kw = a.view.keys_w;
        const double ppt = a.view.px_per_tick(ppq);
        const float pst = a.view.px_per_semitone;
        const float gx0 = origin.x + kw, gy0 = origin.y + top, gx1 = origin.x + avail.x, gy1 = origin.y + avail.y;
        auto X = [&](double tick) { return static_cast<float>(gx0 + (tick - a.view.scroll_tick) * ppt); };
        auto TickAt = [&](float x) { return static_cast<Tick>(std::llround(a.view.scroll_tick + (x - gx0) / ppt)); };
        auto Y = [&](int pitch) { return gy0 + (a.view.top_pitch - static_cast<float>(pitch)) * pst; };
        auto PitchAt = [&](float y) { return static_cast<int>(std::floor(a.view.top_pitch - (y - gy0) / pst)) ; };
        const int vis_rows = static_cast<int>((gy1 - gy0) / pst) + 2;
        a.view.top_pitch = std::clamp(a.view.top_pitch, static_cast<float>(vis_rows - 3), 128.f);
        a.view.scroll_tick = std::max(0.0, a.view.scroll_tick);

        ImGui::InvisibleButton("roll_area", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, ImVec2(gx1, gy1), true);

        const Voicebank* vb = a.vb_of(*t);
        dl->AddRectFilled(origin, ImVec2(gx1, gy1), IM_COL32(24, 25, 29, 255));
        const int top_p = static_cast<int>(std::ceil(a.view.top_pitch)), bot_p = top_p - vis_rows;
        for (int p = std::min(127, top_p); p >= std::max(0, bot_p); --p) {
            const float y = Y(p);
            if (y > gy1 || y + pst < gy0) continue;
            ImU32 c = is_black(p) ? IM_COL32(30, 31, 36, 255) : IM_COL32(36, 38, 44, 255);
            if (vb && vb->profile_ok && p >= vb->profile.range_low && p <= vb->profile.range_high) c = is_black(p) ? IM_COL32(36, 40, 50, 255) : IM_COL32(44, 49, 60, 255);
            dl->AddRectFilled(ImVec2(gx0, std::max(y, gy0)), ImVec2(gx1, std::min(y + pst, gy1)), c);
            if (p % 12 == 0) dl->AddLine(ImVec2(gx0, y + pst), ImVec2(gx1, y + pst), IM_COL32(70, 74, 86, 255));
        }
        const Tick g_ticks = grid_ticks(a.grid, ppq);
        const Tick t_left = static_cast<Tick>(a.view.scroll_tick), t_right = TickAt(gx1);
        if (static_cast<double>(g_ticks) * ppt >= 7) for (Tick k = snap_down(t_left, g_ticks); k <= t_right; k += g_ticks) dl->AddLine(ImVec2(X(static_cast<double>(k)), gy0), ImVec2(X(static_cast<double>(k)), gy1), IM_COL32(255, 255, 255, 12));
        dl->AddRectFilled(ImVec2(gx0, origin.y), ImVec2(gx1, gy0), IM_COL32(32, 34, 40, 255));
        for (int bar = std::max(0, tm.bar_at_tick(t_left) - 1);; ++bar) {
            const Tick bs = tm.bar_start_tick(bar); if (bs > t_right) break;
            const auto sig = tm.signature_at_bar(bar); const Tick beat = tm.ticks_per_beat(sig.denominator);
            const float bx = X(static_cast<double>(bs));
            dl->AddLine(ImVec2(bx, origin.y), ImVec2(bx, gy1), IM_COL32(150, 156, 175, 90));
            char lb[24]; std::snprintf(lb, sizeof lb, "%d", bar + 1); dl->AddText(ImVec2(bx + 3, origin.y + 2), IM_COL32(200, 205, 220, 255), lb);
            for (int b = 1; b < sig.numerator; ++b) { const float x = X(static_cast<double>(bs + b * beat)); if (x >= gx0) { dl->AddLine(ImVec2(x, origin.y + 14), ImVec2(x, gy1), IM_COL32(255, 255, 255, 30)); } }
        }
        // tempo / signature markers
        for (auto& tc : tm.tempi()) { const float x = X(static_cast<double>(tc.tick)); if (x >= gx0 && x <= gx1) { char b[32]; std::snprintf(b, sizeof b, "%.0f bpm", tc.bpm); dl->AddText(ImVec2(x + 3, origin.y + loop_h + 6 - 12 + 12), IM_COL32(240, 190, 90, 255), b); } }
        // loop region
        if (a.project.loop.end > a.project.loop.start) {
            const float lx0 = X(static_cast<double>(a.project.loop.start)), lx1 = X(static_cast<double>(a.project.loop.end));
            const ImU32 lc = a.project.loop.enabled ? IM_COL32(90, 200, 120, 110) : IM_COL32(150, 150, 150, 70);
            dl->AddRectFilled(ImVec2(lx0, origin.y + ruler_h), ImVec2(lx1, gy0), lc);
            dl->AddRectFilled(ImVec2(lx0, gy0), ImVec2(lx1, gy1), a.project.loop.enabled ? IM_COL32(90, 200, 120, 18) : IM_COL32(150, 150, 150, 10));
        }

        for (const Note& n : t->notes) {
            const float x0 = X(static_cast<double>(n.tick)), x1 = std::max(x0 + 3, X(static_cast<double>(n.end()))), y0 = Y(n.pitch);
            if (x1 < gx0 || x0 > gx1 || y0 > gy1 || y0 + pst < gy0) continue;
            const bool sel = a.selection.count(n.id) > 0;
            auto fl = a.note_flags.find(n.id); const int flags = fl == a.note_flags.end() ? 0 : fl->second;
            ImU32 fill = sel ? track_color(a.track_index, 1.f) : track_color(a.track_index, 0.78f);
            if (flags & 1) fill = IM_COL32(200, 70, 70, 230);
            else if (flags & 4) fill = IM_COL32(190, 120, 50, 230);
            else if (flags & 2) fill = sel ? fill : track_color(a.track_index, 0.5f);
            const ImVec2 r0(x0, y0 + 1), r1(x1 - 1, y0 + pst - 1);
            dl->AddRectFilled(r0, r1, fill, 3.f);
            dl->AddRect(r0, r1, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 120), 3.f, 0, sel ? 2.f : 1.f);
            if (x1 - x0 > 14) {
                dl->PushClipRect(r0, r1, true);
                const auto& an = a.analysis_of(n, vb);
                std::string txt = n.lyric.empty() ? "?" : n.lyric;
                dl->AddText(ImVec2(x0 + 4, y0 + (pst - ImGui::GetFontSize()) * 0.5f), IM_COL32(10, 12, 18, 255), txt.c_str());
                const float tw = ImGui::CalcTextSize(txt.c_str()).x;
                if (!an.phoneme_text.empty() && x1 - x0 > tw + 40 && pst >= 14) dl->AddText(ImVec2(x0 + 10 + tw, y0 + (pst - ImGui::GetFontSize()) * 0.5f), IM_COL32(10, 12, 18, 140), an.phoneme_text.c_str());
                dl->PopClipRect();
            }
            if (!n.phonemes.empty()) dl->AddCircleFilled(ImVec2(x1 - 7, y0 + 5), 2.5f, IM_COL32(255, 240, 120, 255));     // pronunciation override marker
            if (n.offset_ms != 0.0) dl->AddTriangleFilled(ImVec2(x0, y0 + pst - 1), ImVec2(x0 + 6, y0 + pst - 1), ImVec2(x0, y0 + pst - 7), IM_COL32(120, 220, 255, 255));
        }
        // rubber band
        if (g.mode == Drag::Rubber) {
            dl->AddRectFilled(g.start, g.cur, IM_COL32(120, 160, 255, 40)); dl->AddRect(g.start, g.cur, IM_COL32(150, 190, 255, 200));
        }
        {
            const float cx = X(static_cast<double>(a.cursor_tick));
            if (cx >= gx0 && cx <= gx1) { dl->AddLine(ImVec2(cx, gy0), ImVec2(cx, gy1), IM_COL32(255, 255, 255, 70), 1.f); dl->AddTriangleFilled(ImVec2(cx - 5, origin.y + 2), ImVec2(cx + 5, origin.y + 2), ImVec2(cx, origin.y + 12), IM_COL32(255, 255, 255, 200)); }
            if (a.player && a.player->transport().state() != TransportState::Stopped) {
                const float px = X(static_cast<double>(tm.sec_to_tick(a.player->transport().position_sec())));
                if (px >= gx0 && px <= gx1) dl->AddLine(ImVec2(px, origin.y), ImVec2(px, gy1), IM_COL32(255, 90, 90, 255), 2.f);
            }
        }
        dl->AddRectFilled(ImVec2(origin.x, gy0), ImVec2(gx0, gy1), IM_COL32(20, 21, 25, 255));
        dl->AddRectFilled(ImVec2(origin.x, origin.y), ImVec2(gx0, gy0), IM_COL32(28, 30, 35, 255));
        for (int p = std::min(127, top_p); p >= std::max(0, bot_p); --p) {
            const float y = Y(p); if (y > gy1 || y + pst < gy0) continue;
            const bool bk = is_black(p);
            dl->AddRectFilled(ImVec2(origin.x + 1, std::max(y, gy0)), ImVec2(bk ? gx0 - 14 : gx0 - 1, std::min(y + pst - 1, gy1)), bk ? IM_COL32(18, 18, 20, 255) : IM_COL32(215, 217, 222, 255));
            if (p % 12 == 0) dl->AddText(ImVec2(origin.x + 4, y + (pst - ImGui::GetFontSize()) * 0.5f), IM_COL32(40, 42, 50, 255), pitch_name(p));
        }
        dl->PopClipRect();

        const ImVec2 mp = io.MousePos;
        const bool in_grid = mp.x >= gx0 && mp.x <= gx1 && mp.y >= gy0 && mp.y <= gy1;
        const bool in_ruler = mp.x >= gx0 && mp.x <= gx1 && mp.y >= origin.y && mp.y < origin.y + ruler_h;
        const bool in_loop = mp.x >= gx0 && mp.x <= gx1 && mp.y >= origin.y + ruler_h && mp.y < gy0;
        const bool alt = io.KeyAlt;
        auto snapped = [&](Tick x) { return alt ? x : a.snap(x); };
        auto hit_note = [&](ImVec2 p, int* edge) -> Note* {
            for (auto it = t->notes.rbegin(); it != t->notes.rend(); ++it) {
                const float x0 = X(static_cast<double>(it->tick)), x1 = std::max(x0 + 3, X(static_cast<double>(it->end()))), y0 = Y(it->pitch);
                if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y0 + pst) {
                    *edge = (x1 - x0 > 16 && p.x > x1 - 6) ? 1 : ((x1 - x0 > 16 && p.x < x0 + 5) ? -1 : 0);
                    return &*it;
                }
            }
            return nullptr;
        };

        if (hovered && !io.WantTextInput) {
            // wheel: scroll / zoom
            if (io.MouseWheel != 0.f || io.MouseWheelH != 0.f) {
                if (io.KeyCtrl && io.KeyShift) { a.view.px_per_semitone = std::clamp(a.view.px_per_semitone * (1.f + 0.1f * io.MouseWheel), 8.f, 36.f); }
                else if (io.KeyCtrl) {
                    const double anchor = a.view.scroll_tick + (mp.x - gx0) / ppt;
                    a.view.px_per_quarter = std::clamp(a.view.px_per_quarter * (1.f + 0.12f * io.MouseWheel), 20.f, 400.f);
                    a.view.scroll_tick = std::max(0.0, anchor - (mp.x - gx0) / a.view.px_per_tick(ppq));
                }
                else if (io.KeyShift || io.MouseWheelH != 0.f) a.view.scroll_tick = std::max(0.0, a.view.scroll_tick - static_cast<double>(io.MouseWheel + io.MouseWheelH) * 60.0 / ppt);
                else a.view.top_pitch = std::clamp(a.view.top_pitch + io.MouseWheel * 3.f, static_cast<float>(vis_rows - 3), 128.f);
            }
        }
        if (g.mode == Drag::None && hovered) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { g = DragState(); g.mode = Drag::Pan; g.start = mp; g.pan_scroll_tick = a.view.scroll_tick; g.pan_scroll_pitch = a.view.top_pitch; }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (in_ruler) { g = DragState(); g.mode = Drag::Scrub; a.cursor_tick = std::max<Tick>(0, snapped(TickAt(mp.x))); if (a.player && !a.playing()) a.player->transport().seek(a.sec(a.cursor_tick)); }
                else if (in_loop) { g = DragState(); g.mode = Drag::Loop; g.loop_anchor = std::max<Tick>(0, snapped(TickAt(mp.x))); }
                else if (in_grid) {
                    int edge = 0; Note* n = hit_note(mp, &edge);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || (io.MouseClickedCount[0] == 2)) {
                        if (n) { a.select_only(n->id); a.edit_lyric_note = n->id; a.edit_lyric_open = true; g_lyric_pos = ImVec2(X(static_cast<double>(n->tick)), Y(n->pitch) + pst);
                            std::snprintf(g_lyric_buf, sizeof g_lyric_buf, "%s", n->lyric.c_str()); ImGui::OpenPopup("##lyric_edit"); }
                        else { Note nn; nn.tick = std::max<Tick>(0, snapped(TickAt(mp.x))); nn.length = g_ticks; nn.pitch = std::clamp(PitchAt(mp.y), 0, 127); nn.lyric = a.default_lyric;
                            NoteId id = a.edit("Add note").add_note(*a.cur_track(), nn); a.select_only(id); }
                    } else if (n) {
                        if (io.KeyCtrl || io.KeyShift) { if (a.selection.count(n->id)) a.selection.erase(n->id); else a.selection.insert(n->id); }
                        else if (!a.selection.count(n->id)) a.select_only(n->id);
                        g = DragState(); g.mode = edge > 0 ? Drag::ResizeR : edge < 0 ? Drag::ResizeL : Drag::Move; g.start = mp; g.grab = n->id; g.ids = a.selection; g.snapshot = *t;
                        if (!a.selection.count(n->id)) g.mode = Drag::None;
                    } else if (a.tool == Tool::Draw) {
                        Note nn; nn.tick = std::max<Tick>(0, snapped(snap_down(TickAt(mp.x), alt ? 1 : std::max<Tick>(1, a.snap_enabled ? g_ticks : 1)))); nn.length = g_ticks;
                        nn.pitch = std::clamp(PitchAt(mp.y), 0, 127); nn.lyric = a.default_lyric;
                        NoteId id = a.edit("Add note").add_note(*a.cur_track(), nn); a.select_only(id);
                        t = a.cur_track(); g = DragState(); g.mode = Drag::ResizeR; g.start = mp; g.grab = id; g.ids = { id }; g.snapshot = *t; g.recorded = true;
                    } else {
                        if (!io.KeyCtrl && !io.KeyShift) a.selection.clear();
                        g = DragState(); g.mode = Drag::Rubber; g.start = mp; g.cur = mp; g.additive = io.KeyCtrl || io.KeyShift; g.ids = a.selection;
                    }
                }
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && in_grid) {
                int edge = 0; Note* n = hit_note(mp, &edge);
                if (n && !a.selection.count(n->id)) a.select_only(n->id);
                if (!n) a.selection.clear();
                g_lyric_pos = mp; ImGui::OpenPopup("roll_ctx");
            }
        }
        // active drags
        if (g.mode != Drag::None) {
            g.cur = mp;
            if (!ImGui::IsMouseDown(g.mode == Drag::Pan ? ImGuiMouseButton_Middle : ImGuiMouseButton_Left)) {
                if (g.mode == Drag::Rubber) {
                    const float xa = std::min(g.start.x, g.cur.x), xb = std::max(g.start.x, g.cur.x), ya = std::min(g.start.y, g.cur.y), yb = std::max(g.start.y, g.cur.y);
                    a.selection = g.additive ? g.ids : std::set<NoteId>();
                    for (auto& n : t->notes) { const float x0 = X(static_cast<double>(n.tick)), x1 = X(static_cast<double>(n.end())), y0 = Y(n.pitch); if (x1 >= xa && x0 <= xb && y0 + pst >= ya && y0 <= yb) a.selection.insert(n.id); }
                }
                a.history.break_coalescing(); g = DragState();
            } else {
                switch (g.mode) {
                    case Drag::Pan: a.view.scroll_tick = std::max(0.0, g.pan_scroll_tick - (mp.x - g.start.x) / ppt); a.view.top_pitch = g.pan_scroll_pitch + (mp.y - g.start.y) / pst; break;
                    case Drag::Scrub: a.cursor_tick = std::max<Tick>(0, snapped(TickAt(mp.x))); if (a.player && !a.playing()) a.player->transport().seek(a.sec(a.cursor_tick)); break;
                    case Drag::Loop: {
                        Tick c = std::max<Tick>(0, snapped(TickAt(mp.x)));
                        if (std::abs(static_cast<double>(c - g.loop_anchor)) * ppt > 3) {
                            if (!g.recorded) { a.edit("Set loop"); g.recorded = true; }
                            a.project.loop.start = std::min(c, g.loop_anchor); a.project.loop.end = std::max(c, g.loop_anchor); a.project.loop.enabled = true; a.sync_loop();
                        } break; }
                    case Drag::Move: case Drag::ResizeR: case Drag::ResizeL: {
                        if (!g.recorded && std::fabs(mp.x - g.start.x) + std::fabs(mp.y - g.start.y) < 3.f) break;
                        if (!g.recorded) { t->notes = g.snapshot.notes; a.edit(g.mode == Drag::Move ? "Move notes" : "Resize notes", "roll-drag"); g.recorded = true; t = a.cur_track(); }
                        t->notes = g.snapshot.notes;
                        const Note* gn = g.snapshot.find_note(g.grab); if (!gn) break;
                        const Tick raw_dt = static_cast<Tick>(std::llround((mp.x - g.start.x) / ppt));
                        const Tick min_len = std::max<Tick>(1, a.snap_enabled && !alt ? g_ticks : 1);
                        if (g.mode == Drag::Move) {
                            Tick dt = snapped(gn->tick + raw_dt) - gn->tick;
                            int dp = -static_cast<int>(std::lround((mp.y - g.start.y) / pst));
                            Tick min_tick = gn->tick; int lo = 127, hi = 0;
                            for (auto id : g.ids) if (auto* n = g.snapshot.find_note(id)) { min_tick = std::min(min_tick, n->tick); lo = std::min(lo, n->pitch); hi = std::max(hi, n->pitch); }
                            dt = std::max(dt, -min_tick); dp = std::clamp(dp, -lo, 127 - hi);
                            for (auto id : g.ids) if (Note* n = t->find_note(id)) { n->tick += dt; n->pitch += dp; }
                        } else if (g.mode == Drag::ResizeR) {
                            const Tick new_end = snapped(gn->end() + raw_dt); const Tick dlen = new_end - gn->end();
                            for (auto id : g.ids) if (Note* n = t->find_note(id)) n->length = std::max(min_len, g.snapshot.find_note(id)->length + dlen);
                        } else {
                            Tick nt = std::max<Tick>(0, snapped(gn->tick + raw_dt)); nt = std::min(nt, gn->end() - min_len); const Tick dt = nt - gn->tick;
                            for (auto id : g.ids) if (Note* n = t->find_note(id)) { const Note* o = g.snapshot.find_note(id); n->tick = o->tick + dt; n->length = std::max(min_len, o->length - dt); }
                        }
                        t->resolve_overlaps(g.ids);
                        break; }
                    default: break;
                }
            }
        }

        // follow the playhead while playing
        if (a.follow_playhead && a.playing() && g.mode == Drag::None) {
            const Tick ph = a.playhead_tick(); const float px = X(static_cast<double>(ph));
            if (px > gx1 - 40 || px < gx0) a.view.scroll_tick = std::max(0.0, static_cast<double>(ph) - 40.0 / ppt);
        }

        if (a.edit_lyric_open) {
            ImGui::SetNextWindowPos(g_lyric_pos);
            if (ImGui::BeginPopup("##lyric_edit")) {
                ImGui::TextDisabled("Lyric (kana, romaji or {phonemes}). Use spaces to spread over the next notes.");
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(320);
                const bool enter = ImGui::InputText("##lyr", g_lyric_buf, sizeof g_lyric_buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool tab = ImGui::IsKeyPressed(ImGuiKey_Tab);
                if (enter || tab) {
                    Track* tt = a.cur_track(); std::string txt = g_lyric_buf; bool sep = txt.find_first_of(" |,/") != std::string::npos;
                    auto units = split_lyric_units(txt);
                    a.edit("Edit lyric"); tt = a.cur_track();
                    size_t idx = 0; for (; idx < tt->notes.size(); ++idx) if (tt->notes[idx].id == a.edit_lyric_note) break;
                    if (idx < tt->notes.size()) {
                        if (sep && units.size() > 1) { for (size_t k = 0; k < units.size() && idx + k < tt->notes.size(); ++k) { tt->notes[idx + k].lyric = units[k]; tt->notes[idx + k].phonemes.clear(); } }
                        else { tt->notes[idx].lyric = txt; tt->notes[idx].phonemes.clear(); }
                        if (!txt.empty()) a.default_lyric = units.empty() ? txt : units.front();
                        if (tab && idx + 1 < tt->notes.size()) {
                            NoteId nid = tt->notes[idx + 1].id; a.select_only(nid); a.edit_lyric_note = nid;
                            std::snprintf(g_lyric_buf, sizeof g_lyric_buf, "%s", tt->notes[idx + 1].lyric.c_str()); ImGui::SetKeyboardFocusHere(-1);
                        } else { a.edit_lyric_open = false; ImGui::CloseCurrentPopup(); }
                    } else { a.edit_lyric_open = false; ImGui::CloseCurrentPopup(); }
                }
                ImGui::EndPopup();
            } else a.edit_lyric_open = false;
        }

        if (ImGui::BeginPopup("roll_ctx")) {
            const bool has_sel = !a.selection.empty();
            if (ImGui::MenuItem("Edit lyric...", "double-click", false, a.selection.size() == 1)) {
                a.edit_lyric_note = *a.selection.begin(); a.edit_lyric_open = true; if (Note* n = t->find_note(a.edit_lyric_note)) std::snprintf(g_lyric_buf, sizeof g_lyric_buf, "%s", n->lyric.c_str());
                g_lyric_pos = ImGui::GetMousePosOnOpeningCurrentPopup(); ImGui::CloseCurrentPopup(); ImGui::OpenPopup("##lyric_edit");
            }
            if (ImGui::MenuItem("Preview selection", "Enter", false, has_sel)) a.preview_selection();
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_sel)) a.copy_selection(false);
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, has_sel)) a.copy_selection(true);
            if (ImGui::MenuItem("Paste here", "Ctrl+V", false, !a.clipboard.empty())) a.paste_at(std::max<Tick>(0, snapped(TickAt(g_lyric_pos.x))));
            if (ImGui::MenuItem("Delete", "Del", false, has_sel)) a.delete_selection();
            if (ImGui::MenuItem("Duplicate", nullptr, false, has_sel)) {
                a.copy_selection(false); Tick end = 0, start = a.clipboard_origin; for (auto& n : a.clipboard) end = std::max(end, n.end()); a.paste_at(end); (void)start;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Transpose", has_sel)) {
                if (ImGui::MenuItem("+1 semitone")) a.transpose_selection(1);
                if (ImGui::MenuItem("-1 semitone")) a.transpose_selection(-1);
                if (ImGui::MenuItem("+1 octave")) a.transpose_selection(12);
                if (ImGui::MenuItem("-1 octave")) a.transpose_selection(-12);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Quantize to grid", "Q", false, has_sel)) a.quantize_selection();
            if (ImGui::MenuItem("Set loop to selection", nullptr, false, has_sel)) {
                auto sel = a.selected_notes(); Tick s0 = sel.front()->tick, e0 = sel.front()->end(); for (auto* n : sel) { s0 = std::min(s0, n->tick); e0 = std::max(e0, n->end()); }
                a.edit("Set loop"); a.project.loop = { true, s0, e0 }; a.sync_loop();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Add note here", nullptr, false, true)) {
                Note nn; nn.tick = std::max<Tick>(0, snapped(TickAt(g_lyric_pos.x))); nn.length = g_ticks; nn.pitch = std::clamp(PitchAt(g_lyric_pos.y), 0, 127); nn.lyric = a.default_lyric;
                NoteId id = a.edit("Add note").add_note(*a.cur_track(), nn); a.select_only(id);
            }
            if (ImGui::MenuItem("Select all", "Ctrl+A")) a.select_all();
            ImGui::EndPopup();
        }

        // cursor hints
        if (hovered && in_grid && g.mode == Drag::None) { int e = 0; if (hit_note(mp, &e) && e != 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
        if (hovered && in_grid && ImGui::IsItemHovered() && g.mode == Drag::None) {
            const int p = PitchAt(mp.y); ImGui::SetTooltip("%s  |  %s", pitch_name(std::clamp(p, 0, 127)), tick_label(tm, std::max<Tick>(0, TickAt(mp.x))).c_str());
        }
        ImGui::End();
    }
}
