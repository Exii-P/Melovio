// Melovio Studio: shared application state and the interfaces between its panels.
#pragma once
#include "audio/sdl_audio_output.hpp"
#include "core/config.hpp"
#include "core/export.hpp"
#include "core/history.hpp"
#include "core/project_io.hpp"
#include "core/pronunciation.hpp"
#include "core/transport.hpp"
#include "core/voicebank_library.hpp"
#include "imgui.h"
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <thread>

namespace melovio::ui {
    enum class FileMode { Open, Save, Folder };
    struct FileDialog {
        bool open = false;
        std::string title, id;
        FileMode mode = FileMode::Open;
        std::vector<std::string> exts;               // e.g. {".mvp"}; empty = all files
        fs::path dir;
        char name[512] = {};
        std::string error;
        std::function<void(const fs::path&)> on_pick;
        bool request_open = false;
        void show(const std::string& id_, const std::string& title_, FileMode m, std::vector<std::string> exts_, const fs::path& start_dir,
                  const std::string& default_name, std::function<void(const fs::path&)> cb);
        void draw();                                 // call every frame
    };

    enum class Tool { Select, Draw };
    struct RollView {
        float px_per_quarter = 96.f;                 // horizontal zoom
        float px_per_semitone = 16.f;                // vertical zoom
        double scroll_tick = 0;                      // leftmost visible tick
        float top_pitch = 88.f;                      // pitch at the top edge (fractional)
        float keys_w = 54.f;                         // width of the piano-key column
        double px_per_tick(int ppq) const { return static_cast<double>(px_per_quarter) / ppq; }
    };

    struct Toast { std::string text; double until = 0; Severity sev = Severity::Info; };

    struct ExportJob {
        std::thread worker;
        CancelToken cancel;
        std::atomic<double> progress{ 0 };
        std::atomic<bool> running{ false }, finished{ false };
        std::string result_text; bool ok = false; std::vector<Diagnostic> diags;
        std::mutex m;
    };

    struct App {
        Project project;
        History history;
        fs::path project_path;                       // empty = never saved
        bool dirty = false;
        AppConfig cfg;
        fs::path exe_dir, cfg_path, autosave_dir;
        VoicebankLibrary lib;
        std::vector<VoicebankResolution> resolutions;
        std::map<std::string, Voicebank> banks;      // resolved snapshot for rendering
        PronunciationEngine pron;
        std::shared_ptr<UserDictionary> dict = std::make_shared<UserDictionary>();
        std::unique_ptr<AutosaveStore> autosave;
        double last_autosave = 0;
        std::unique_ptr<SdlAudioOutput> audio;
        std::unique_ptr<Player> player;
        std::string audio_error;
        bool jp_font_loaded = false;

        int track_index = 0;
        std::set<NoteId> selection;
        GridDivision grid{ 16, false };
        Tool tool = Tool::Select;
        RollView view;
        Tick cursor_tick = 0;
        std::vector<Note> clipboard; Tick clipboard_origin = 0;
        std::string default_lyric = "あ";
        std::string lane_param = "pitch";
        bool follow_playhead = true;
        bool snap_enabled = true;
        bool show_ghost = true;          // draw the other tracks' notes faintly in the piano roll
        NoteId edit_lyric_note = 0; bool edit_lyric_open = false;

        std::vector<Diagnostic> diagnostics;
        bool diag_stale = true; double diag_time = 0;
        std::map<NoteId, int> note_flags;            // bit0 = unpronounceable, bit1 = outside comfortable range, bit2 = outside hard range
        std::map<std::string, LyricAnalysis> analysis_cache;

        bool show_tracks = true, show_voicebanks = true, show_roll = true, show_params = true, show_phonemes = true, show_props = true,
             show_workshop = false, show_log = false, show_demo = false;
        bool open_export = false, open_settings = false, open_about = false, open_missing = false, open_recovery = false, open_quit = false, open_new_confirm = false;
        std::function<void()> after_unsaved;         // continuation once the user decided about unsaved changes
        FileDialog file_dialog;
        std::deque<Toast> toasts;
        std::vector<std::string> log;
        bool quit_requested = false;
        std::vector<AutosaveStore::Candidate> recovery_candidates;
        ExportSettings export_settings;
        ExportJob export_job;
        std::string export_path_text;
        std::string pending_select_voicebank;        // voicebank manager selection
        double now = 0;

        void init(const fs::path& exe, const std::string& open_arg);
        void shutdown();
        void new_project();
        void open_project(const fs::path& p);
        bool save_project_now();                     // false if it needed a Save As dialog
        void save_project_as();
        void request_close_project(std::function<void()> then);
        void load_project_file(const fs::path& p);
        void adopt_project(Project&& p, const fs::path& path);
        void refresh_voicebanks();
        void remember_recent(const fs::path& p);

        Track* cur_track() { return project.tracks.empty() ? nullptr : &project.tracks[static_cast<size_t>(std::clamp(track_index, 0, static_cast<int>(project.tracks.size()) - 1))]; }
        Project& edit(const std::string& label, const std::string& coalesce_key = "");
        void touch() { dirty = true; diag_stale = true; if (player) player->mark_dirty(); }
        const Voicebank* vb_of(const Track& t) const { auto it = banks.find(t.voicebank_id); return it == banks.end() ? nullptr : &it->second; }
        const LyricAnalysis& analysis_of(const Note& n, const Voicebank* vb);
        void refresh_diagnostics();
        void undo(); void redo();
        void select_only(NoteId id) { selection.clear(); selection.insert(id); }
        std::vector<Note*> selected_notes();
        void copy_selection(bool cut); void paste_at(Tick t);
        void duplicate_selection();      // copies the selection right after itself (Ctrl+D)
        void extend_selection();         // stretches each selected note to the next note (or the next grid line if it is the last)
        void delete_selection(); void transpose_selection(int semis); void nudge_selection(Tick dt); void quantize_selection();
        void select_all();
        Tick snap(Tick t) const { return snap_enabled ? snap_to_grid(t, grid_ticks(grid, project.tempo_map.ppq())) : t; }

        void toggle_play(); void stop(); void play_from_start();
        void preview_selection();
        double sec(Tick t) const { return project.tempo_map.tick_to_sec(t); }
        Tick playhead_tick() const;
        bool playing() const { return player && player->transport().state() == TransportState::Playing; }
        void sync_loop();

        void start_export(const fs::path& out);

        void toast(const std::string& t, Severity s = Severity::Info, double secs = 4.0);
        void add_log(const std::string& s);
        std::string window_title() const;
    };

    void draw_piano_roll(App& a);
    void draw_param_lanes(App& a);
    void draw_tracks(App& a);
    void draw_properties(App& a);
    void draw_phonemes(App& a);
    void draw_voicebanks(App& a);
    void draw_workshop(App& a);
    void begin_midi_import(App& a);          // asks for a .mid file, then shows the import dialog
    void load_midi_for_import(App& a, const fs::path& file);   // same as picking the file in the dialog
    void begin_midi_export(App& a);
    void draw_midi_dialogs(App& a);
    void draw_dialogs(App& a);
    void draw_transport_bar(App& a);
    void draw_main_menu(App& a);
    void handle_global_shortcuts(App& a);
    void apply_theme(float scale);
    bool load_japanese_font(float size);

    // small shared UI helpers
    std::string tick_label(const TempoMap& tm, Tick t);                 // "bar.beat.tick" (1-based bar/beat)
    ImU32 track_color(int index, float alpha = 1.f);
    const char* pitch_name(int midi);
}
