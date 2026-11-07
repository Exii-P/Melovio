// Standard MIDI File reading/writing and conversion to and from Melovio projects.
// Notes become one monophonic Melovio track per MIDI track; lyrics come from Lyric (FF 05) meta events.
#pragma once
#include "project.hpp"
#include "util.hpp"

namespace melovio {
    struct MidiNote { Tick tick = 0; Tick length = 1; int pitch = 60; int channel = 0; int velocity = 100; std::string lyric; };
    struct MidiTrack { std::string name; std::vector<MidiNote> notes; size_t lyric_count = 0; };
    struct MidiTempo { Tick tick = 0; double bpm = 120.0; };
    struct MidiSignature { Tick tick = 0; int numerator = 4; int denominator = 4; };

    // Everything is in the file's own ticks (`ppq`); conversion to project ticks happens in import_midi.
    struct MidiFile {
        int format = 1;
        int ppq = 480;
        std::vector<MidiTempo> tempi;
        std::vector<MidiSignature> signatures;
        std::vector<MidiTrack> tracks;                // only tracks that contain notes (format 0 is split by channel)
        std::vector<Diagnostic> diagnostics;          // problems found while parsing (all warnings; hard failures are errors)
    };

    Result<MidiFile> parse_midi(const std::string& bytes);
    Result<MidiFile> read_midi_file(const fs::path& path);

    struct MidiImportOptions {
        std::vector<size_t> tracks;                   // indices into MidiFile::tracks to import
        std::string voicebank_id = "yune";
        std::string default_lyric = "あ";             // for notes without a lyric event
        bool import_tempo = false;                    // replace the project's tempo map and time signatures with the file's
    };
    struct MidiImportSummary { size_t tracks_added = 0, notes_added = 0, notes_lost_to_overlap = 0, notes_without_lyric = 0; };

    // Appends the chosen tracks to `project`. Chords and overlaps are reduced to a monophonic line (the highest note of a chord wins).
    Result<MidiImportSummary> import_midi(Project& project, const MidiFile& file, const MidiImportOptions& options, std::vector<Diagnostic>* diagnostics = nullptr);

    struct MidiExportOptions {
        std::vector<std::string> track_ids;           // empty = all tracks
        bool include_muted = true;
    };
    // Format 1: a conductor track (tempo, time signature) plus one track per Melovio track with note and lyric (UTF-8) events.
    Result<std::string> encode_midi(const Project& project, const MidiExportOptions& options = {});
    Status write_midi_file(const fs::path& path, const Project& project, const MidiExportOptions& options = {});
}
