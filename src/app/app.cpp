#include "app.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace melovio::ui {
    void App::init(const fs::path& exe, const std::string& open_arg) {
        exe_dir = exe;
        cfg_path = config_file_path();
        std::string warn; cfg = load_config_or_default(cfg_path, &warn);
        if (!warn.empty()) add_log("settings: " + warn + " (using defaults)");
        export_settings = cfg.export_defaults;
        autosave_dir = default_config_dir() / "autosave";
        autosave = std::make_unique<AutosaveStore>(autosave_dir);
        setup_default_library(lib, exe_dir, &cfg);
        // user reading dictionary (optional): <config>/user_dictionary.tsv
        const fs::path dictf = default_config_dir() / "user_dictionary.tsv";
        std::error_code ec;
        if (fs::exists(dictf, ec)) { Status s = dict->load(dictf); if (!s) add_log("user dictionary: " + s.message); }
        pron.set_reading_provider(dict);

        audio = std::make_unique<SdlAudioOutput>();
        player = std::make_unique<Player>(*audio, std::map<std::string, Voicebank>{});
        if (!player->open_device(cfg.audio_sample_rate, &audio_error)) {
            add_log("audio device unavailable: " + audio_error + " (editing and export still work)");
            toast("No audio device: " + audio_error, Severity::Warning, 8);
        }
        new_project();
        if (!open_arg.empty()) load_project_file(path_from_utf8(open_arg));
        else { recovery_candidates = autosave->find_recoverable(); if (!recovery_candidates.empty()) open_recovery = true; }
        last_autosave = 0;
    }

    void App::shutdown() {
        if (export_job.running) { export_job.cancel.cancel(); }
        if (export_job.worker.joinable()) export_job.worker.join();
        if (player) player->stop();
        autosave->discard(project_path);            // clean exit (or the user chose to discard): nothing to recover
        cfg.export_defaults = export_settings;
        save_config(cfg, cfg_path);
        player.reset(); audio.reset();
    }

    void App::new_project() {
        Project p;
        const Voicebank* first = lib.find("yune");
        if (!first && !lib.all().empty()) first = &lib.all().front();
        Track& t = p.add_track("Vocal 1", first ? first->manifest.id : "yune");
        if (first) p.ensure_voicebank_ref(first->manifest.id, first->manifest.version, first->manifest.name);
        (void)t;
        adopt_project(std::move(p), {});
    }

    void App::adopt_project(Project&& p, const fs::path& path) {
        player->stop();
        project = std::move(p); project_path = path; history.clear(); selection.clear(); track_index = 0; cursor_tick = 0;
        dirty = false; diag_stale = true; analysis_cache.clear(); note_flags.clear();
        view.scroll_tick = 0; view.top_pitch = 88.f;
        // center the view on the notes if there are any
        if (Track* t = cur_track(); t && !t->notes.empty()) { int lo = 127, hi = 0; for (auto& n : t->notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); } view.top_pitch = static_cast<float>(std::min(108, hi + 8)); }
        refresh_voicebanks();
        if (player) player->mark_dirty();
    }

    void App::refresh_voicebanks() {
        const fs::path dir = project_path.empty() ? fs::current_path() : (project_path.has_parent_path() ? project_path.parent_path() : fs::current_path());
        resolutions = lib.resolve(project, dir);
        banks = snapshot_for_project(resolutions);
        for (auto& r : resolutions) if (r.ok() && r.voicebank) project.ensure_voicebank_ref(r.id, r.voicebank->manifest.version, r.voicebank->manifest.name);
        if (player) player->update_voicebanks(banks);
        diag_stale = true;
    }

    void App::load_project_file(const fs::path& p) {
        std::vector<Diagnostic> warn;
        auto r = load_project(p, &warn);
        if (!r) { toast("Cannot open project: " + r.error, Severity::Error, 10); add_log("open failed: " + r.error); return; }
        adopt_project(std::move(*r), p);
        remember_recent(p);
        for (auto& w : warn) add_log(w.message);
        bool missing = false; for (auto& x : resolutions) if (!x.ok()) missing = true;
        if (missing) open_missing = true;
        toast("Opened " + path_to_utf8(p.filename()));
    }

    void App::open_project(const fs::path& p) { request_close_project([this, p] { load_project_file(p); }); }

    void App::remember_recent(const fs::path& p) { cfg.add_recent(p); cfg.last_project_dir = path_to_utf8(p.parent_path()); save_config(cfg, cfg_path); }

    bool App::save_project_now() {
        if (project_path.empty()) { save_project_as(); return false; }
        std::vector<Diagnostic> w;
        Status s = save_project(project, project_path, &w);
        if (!s) { toast("Save failed: " + s.message, Severity::Error, 10); return false; }
        for (auto& x : w) add_log(x.message);
        dirty = false; autosave->discard(project_path); remember_recent(project_path);
        toast("Saved");
        return true;
    }

    void App::save_project_as() {
        file_dialog.show("saveas", "Save project as", FileMode::Save, { kProjectExtension }, cfg.last_project_dir.empty() ? fs::current_path() : path_from_utf8(cfg.last_project_dir),
            project.meta.title + kProjectExtension, [this](const fs::path& p) {
                const fs::path old = project_path; project_path = p;
                if (save_project_now()) { if (!old.empty()) autosave->discard(old); refresh_voicebanks(); if (after_unsaved) { auto f = std::move(after_unsaved); after_unsaved = nullptr; f(); } }
                else project_path = old;
            });
    }

    void App::request_close_project(std::function<void()> then) {
        if (!dirty) { then(); return; }
        after_unsaved = std::move(then); open_new_confirm = true;
    }

    Project& App::edit(const std::string& label, const std::string& key) { history.record(project, label, key); touch(); return project; }

    void App::undo() { if (history.undo(project)) { touch(); analysis_cache.clear(); const Track* t = cur_track(); (void)t; std::set<NoteId> keep; if (Track* c = cur_track()) for (auto& n : c->notes) if (selection.count(n.id)) keep.insert(n.id); selection = keep; if (track_index >= static_cast<int>(project.tracks.size())) track_index = 0; refresh_voicebanks(); } }
    void App::redo() { if (history.redo(project)) { touch(); std::set<NoteId> keep; if (Track* c = cur_track()) for (auto& n : c->notes) if (selection.count(n.id)) keep.insert(n.id); selection = keep; if (track_index >= static_cast<int>(project.tracks.size())) track_index = 0; refresh_voicebanks(); } }

    std::vector<Note*> App::selected_notes() {
        std::vector<Note*> v; if (Track* t = cur_track()) for (auto& n : t->notes) if (selection.count(n.id)) v.push_back(&n); return v;
    }

    void App::select_all() { selection.clear(); if (Track* t = cur_track()) for (auto& n : t->notes) selection.insert(n.id); }

    void App::copy_selection(bool cut) {
        auto sel = selected_notes(); if (sel.empty()) return;
        clipboard.clear(); clipboard_origin = sel.front()->tick;
        for (auto* n : sel) { clipboard.push_back(*n); clipboard_origin = std::min(clipboard_origin, n->tick); }
        if (cut) delete_selection();
        toast(std::to_string(clipboard.size()) + " note(s) copied");
    }

    void App::paste_at(Tick t) {
        if (clipboard.empty() || !cur_track()) return;
        Project& p = edit("Paste");
        Track& tr = *cur_track();
        std::set<NoteId> fresh;
        for (Note n : clipboard) { n.id = 0; n.tick = std::max<Tick>(0, n.tick - clipboard_origin + t); fresh.insert(p.add_note(tr, n)); }
        selection = fresh;
        // add_note resolves overlaps per note; make sure all pasted notes survive against each other
        tr.resolve_overlaps(fresh);
        std::set<NoteId> alive; for (auto& n : tr.notes) if (fresh.count(n.id)) alive.insert(n.id);
        selection = alive;
    }

    void App::duplicate_selection() {
        auto sel = selected_notes(); if (sel.empty()) return;
        Tick lo = sel.front()->tick, hi = sel.front()->end();
        for (auto* n : sel) { lo = std::min(lo, n->tick); hi = std::max(hi, n->end()); }
        std::vector<Note> copies; for (auto* n : sel) copies.push_back(*n);
        const Tick shift = hi - lo;
        Project& p = edit("Duplicate");
        Track& tr = *cur_track();
        std::set<NoteId> fresh;
        for (Note n : copies) { n.id = 0; n.tick += shift; fresh.insert(p.add_note(tr, n)); }
        tr.resolve_overlaps(fresh);
        std::set<NoteId> alive; for (auto& n : tr.notes) if (fresh.count(n.id)) alive.insert(n.id);
        selection = alive;
    }

    void App::extend_selection() {
        if (selection.empty()) return;
        edit("Extend notes"); Track* t = cur_track();
        const Tick g = std::max<Tick>(1, grid_ticks(grid, project.tempo_map.ppq()));
        for (auto* n : selected_notes()) {
            Tick next = -1;
            for (auto& o : t->notes) if (o.id != n->id && o.tick > n->tick && (next < 0 || o.tick < next)) next = o.tick;
            const Tick target = next >= 0 ? next : (n->end() / g + 1) * g;
            if (target > n->end()) n->length = target - n->tick;
        }
    }

    void App::delete_selection() {
        Track* t = cur_track(); if (!t || selection.empty()) return;
        edit("Delete notes");
        t = cur_track();
        t->notes.erase(std::remove_if(t->notes.begin(), t->notes.end(), [&](const Note& n) { return selection.count(n.id) > 0; }), t->notes.end());
        selection.clear();
    }

    void App::transpose_selection(int semis) {
        if (selection.empty()) return;
        edit("Transpose");
        for (auto* n : selected_notes()) n->pitch = std::clamp(n->pitch + semis, 0, 127);
    }
    void App::nudge_selection(Tick dt) {
        if (selection.empty()) return;
        edit("Move notes"); Track* t = cur_track();
        for (auto* n : selected_notes()) n->tick = std::max<Tick>(0, n->tick + dt);
        t->resolve_overlaps(selection);
    }
    void App::quantize_selection() {
        if (selection.empty()) return;
        edit("Quantize"); Track* t = cur_track(); const Tick g = grid_ticks(grid, project.tempo_map.ppq());
        for (auto* n : selected_notes()) {
            const Tick end = snap_to_grid(n->end(), g); n->tick = snap_to_grid(n->tick, g);
            n->length = std::max<Tick>(g, end - n->tick);
        }
        t->resolve_overlaps(selection);
    }

    const LyricAnalysis& App::analysis_of(const Note& n, const Voicebank* vb) {
        PronunciationOptions po; if (vb && vb->profile_ok) { po.merge_long_vowels = vb->profile.parse_merge_long_vowels; po.cute = vb->profile.parse_cute; }
        std::string phon = n.phonemes;
        if (phon.empty() && vb) { auto m = vb->manifest.phonetic_mappings.find(n.lyric); if (m != vb->manifest.phonetic_mappings.end()) phon = m->second; }
        const std::string key = n.lyric + "\x1f" + phon + (po.merge_long_vowels ? "m" : "-") + (po.cute ? "c" : "-");
        auto it = analysis_cache.find(key);
        if (it != analysis_cache.end()) return it->second;
        if (analysis_cache.size() > 4000) analysis_cache.clear();
        return analysis_cache.emplace(key, pron.analyze_note(n.lyric, phon, po)).first->second;
    }

    void App::refresh_diagnostics() {
        if (!diag_stale || now - diag_time < 0.35) return;
        diag_stale = false; diag_time = now;
        diagnostics = preflight(project, banks, pron);
        note_flags.clear();
        if (const Track* t = cur_track()) {
            const Voicebank* vb = vb_of(*t);
            for (auto& n : t->notes) {
                int f = 0;
                if (!analysis_of(n, vb).pronounceable) f |= 1;
                if (vb && vb->profile_ok) { if (n.pitch < vb->profile.range_low || n.pitch > vb->profile.range_high) f |= 2; if (n.pitch < vb->profile.hard_low || n.pitch > vb->profile.hard_high) f |= 4; }
                if (f) note_flags[n.id] = f;
            }
        }
    }

    Tick App::playhead_tick() const {
        if (player && player->transport().state() != TransportState::Stopped) return project.tempo_map.sec_to_tick(player->transport().position_sec());
        return cursor_tick;
    }

    void App::sync_loop() {
        if (!player) return;
        const bool on = project.loop.enabled && project.loop.end > project.loop.start;
        player->transport().set_loop(on, sec(project.loop.start), sec(project.loop.end));
    }

    void App::toggle_play() {
        if (!player) return;
        sync_loop();
        auto st = player->transport().state();
        if (st == TransportState::Playing) { player->pause(); return; }
        if (player->status() == PlayerStatus::Rendering) return;
        if (st == TransportState::Paused && !player->dirty()) { player->resume_or_play(); return; }
        player->update_voicebanks(banks);
        const double from = st == TransportState::Paused ? player->transport().position_sec() : sec(cursor_tick);
        player->transport().set_return_position(sec(cursor_tick));
        player->render_async(project, true, from);
    }
    void App::stop() { if (!player) return; player->cancel_render(); player->stop(); }
    void App::play_from_start() { cursor_tick = 0; if (player) { player->stop(); } toggle_play(); }

    void App::preview_selection() {
        Track* t = cur_track(); if (!t || !player) return;
        auto sel = selected_notes(); if (sel.empty()) { toast("Select notes to preview", Severity::Info); return; }
        Tick a = sel.front()->tick, b = sel.front()->end();
        for (auto* n : sel) { a = std::min(a, n->tick); b = std::max(b, n->end()); }
        player->update_voicebanks(banks);
        const double t0 = std::max(0.0, sec(a) - 0.25), t1 = sec(b) + 0.5;
        player->preview_async(project, t0, t1);
    }

    void App::start_export(const fs::path& out) {
        if (export_job.running) return;
        if (export_job.worker.joinable()) export_job.worker.join();
        Project snapshot = project; ExportSettings es = export_settings; auto banks_copy = banks;
        export_job.cancel.reset(); export_job.progress = 0; export_job.running = true; export_job.finished = false; export_job.result_text.clear(); export_job.diags.clear(); export_job.ok = false;
        export_job.worker = std::thread([this, snapshot, es, banks_copy, out]() {
            Renderer r(banks_copy, player ? std::make_shared<PhraseCache>() : std::make_shared<PhraseCache>(), std::make_shared<PronunciationEngine>(pron));
            auto res = export_wav(snapshot, r, es, out, [this](double f, const std::string&) { export_job.progress = f; }, &export_job.cancel);
            std::lock_guard<std::mutex> l(export_job.m);
            if (res) { export_job.ok = true; export_job.diags = res->diagnostics;
                char buf[256]; std::snprintf(buf, sizeof buf, "Wrote %s  (%.2f s, peak %.2f dBFS%s)", path_to_utf8(out.filename()).c_str(), res->duration_sec, res->peak > 0 ? 20 * std::log10(res->peak) : -120.0, res->clipped ? ", CLIPPED" : "");
                export_job.result_text = buf; }
            else { export_job.ok = false; export_job.result_text = res.error == "cancelled" ? "Export cancelled" : "Export failed: " + res.error; }
            export_job.running = false; export_job.finished = true;
        });
    }

    void App::toast(const std::string& t, Severity s, double secs) { toasts.push_back({ t, now + secs, s }); add_log(t); if (toasts.size() > 5) toasts.pop_front(); }
    void App::add_log(const std::string& s) { log.push_back(s); if (log.size() > 500) log.erase(log.begin()); }
    std::string App::window_title() const {
        return std::string(dirty ? "* " : "") + (project_path.empty() ? project.meta.title + " (unsaved)" : path_to_utf8(project_path.filename())) + " - Melovio Studio";
    }

    std::string tick_label(const TempoMap& tm, Tick t) {
        auto bb = tm.bar_beat(t); char b[64]; std::snprintf(b, sizeof b, "%d.%d.%03d", bb.bar + 1, bb.beat + 1, static_cast<int>(bb.tick_in_beat)); return b;
    }
    ImU32 track_color(int i, float alpha) {
        static const ImVec4 pal[] = { { 0.36f, 0.62f, 0.95f, 1 }, { 0.95f, 0.55f, 0.35f, 1 }, { 0.45f, 0.82f, 0.55f, 1 }, { 0.82f, 0.48f, 0.90f, 1 }, { 0.93f, 0.80f, 0.35f, 1 }, { 0.35f, 0.82f, 0.85f, 1 } };
        ImVec4 c = pal[static_cast<size_t>(i) % 6]; c.w = alpha; return ImGui::ColorConvertFloat4ToU32(c);
    }
    const char* pitch_name(int midi) {
        static char bufs[6][16]; static int rot = 0; char* buf = bufs[rot++ % 6]; static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf(buf, 16, "%s%d", n[((midi % 12) + 12) % 12], midi / 12 - 1); return buf;
    }
}
