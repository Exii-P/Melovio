// Melovio project data model. Pure data + editing helpers; no I/O, no engine, no GUI.
#pragma once
#include "params.hpp"
#include "timeline.hpp"
#include "util.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace melovio {
    using NoteId = uint64_t;

    struct Note {
        NoteId id = 0;
        Tick tick = 0;                  // start, in project ticks
        Tick length = 480;
        int pitch = 60;                 // MIDI note number
        std::string lyric = "あ";       // as typed: kana, romaji, or "{k a}" phoneme notation
        std::string phonemes;           // pronunciation override (Wauvio phoneme notation). When non-empty it replaces the lyric.
        float velocity = 0.75f;         // 0..1
        double offset_ms = 0.0;         // timing offset applied at render (negative = earlier), does not move the note on the grid
        std::map<std::string, float> params;   // per-note overrides, keys from param_registry() with note_param
        Tick end() const { return tick + length; }
    };

    struct AutomationPoint { Tick tick = 0; float value = 0.f; };
    struct Lane {
        std::string param;                       // key of a registry entry with lane == true
        std::vector<AutomationPoint> points;     // sorted by tick, linearly interpolated, holds first/last value outside
        float value_at(Tick t) const;
        void set_point(Tick t, float v);         // replaces a point at the same tick
        void remove_range(Tick a, Tick b);       // removes points with a <= tick <= b
    };

    struct VoicebankRef {
        std::string id;                          // voicebank id, e.g. "yune"
        std::string version;                     // version the project was last saved with
        std::string name;                        // display name (informational)
        std::string path_hint;                   // OPTIONAL path relative to the project file; never absolute
    };

    struct Track {
        std::string id;
        std::string name = "Vocal";
        std::string voicebank_id = "yune";
        std::string persona;                     // empty = voicebank's default look
        float persona_amount = 1.f;
        std::map<std::string, float> style;      // track-wide Style overrides (same keys as Note::params)
        float natural = 0.f;                     // 0..1 "living voice" amount (Singer::natural); 0 = off
        int natural_take = 1;
        bool auto_expression = false;            // Singer::auto_expression
        float gain_db = 0.f;
        float pan = 0.f;                         // -1..1
        bool mute = false, solo = false;
        std::vector<Note> notes;                 // kept sorted by tick, monophonic (no overlaps)
        std::vector<Lane> lanes;

        Note* find_note(NoteId id);
        const Note* find_note(NoteId id) const;
        Lane* find_lane(const std::string& param);
        Lane& ensure_lane(const std::string& param);
        void sort_notes();
        // Restores the monophonic invariant. Notes in `keep` win over the notes they collide with: a colliding earlier note is shortened,
        // a note that is fully covered is removed. Returns number of notes removed.
        int resolve_overlaps(const std::set<NoteId>& keep = {});
        Tick end_tick() const;
    };

    struct LoopRegion { bool enabled = false; Tick start = 0; Tick end = 0; };

    struct ProjectMeta {
        std::string title = "Untitled";
        std::string composer, lyricist, comment;
        std::string created, modified;           // ISO-8601 UTC
    };

    struct Project {
        ProjectMeta meta;
        TempoMap tempo_map;
        LoopRegion loop;
        int sample_rate = 44100;
        std::vector<VoicebankRef> voicebanks;
        std::vector<Track> tracks;
        NoteId next_note_id = 1;

        Project();
        Track& add_track(const std::string& name, const std::string& voicebank_id);
        void remove_track(const std::string& track_id);
        Track* find_track(const std::string& id);
        const Track* find_track(const std::string& id) const;
        NoteId alloc_note_id() { return next_note_id++; }
        // Adds a note (id assigned if 0) and restores invariants with the new note winning. Returns its id.
        NoteId add_note(Track& t, Note n);
        VoicebankRef* find_voicebank_ref(const std::string& id);
        const VoicebankRef* find_voicebank_ref(const std::string& id) const;
        void ensure_voicebank_ref(const std::string& id, const std::string& version, const std::string& name);
        Tick end_tick() const;                   // end of the last note over all tracks
        double duration_sec() const { return tempo_map.tick_to_sec(end_tick()); }
    };

    // Structural problems that are independent of any voicebank (overlaps, bad pitches, empty lyrics, unknown params ...).
    std::vector<Diagnostic> validate_project(const Project& p);
}
