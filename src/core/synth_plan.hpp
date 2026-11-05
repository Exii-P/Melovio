// Turns project data (notes in ticks, lyrics, parameters, lanes) into engine-ready plans (seconds, engine lyrics, Style overrides).
// This is steps 1-5 of the synthesis pipeline in docs/SYNTHESIS_PIPELINE.md; engine_bridge::render_plan is step 6-7.
#pragma once
#include "engine_bridge.hpp"
#include "project.hpp"
#include "pronunciation.hpp"
#include "voicebank.hpp"

namespace melovio {
    // A phrase is a run of notes separated from the next run by a long rest. Phrases are rendered (and cached) independently.
    struct PhraseSpan {
        size_t first = 0, last = 0;          // inclusive note indices in the track
        double start_sec = 0, end_sec = 0;   // audible extent on the project timeline (including per-note timing offsets)
    };
    constexpr double kDefaultPhraseGapSec = 1.0;
    std::vector<PhraseSpan> split_phrases(const Track& t, const TempoMap& tm, double gap_sec = kDefaultPhraseGapSec);

    struct BuiltPlan {
        TrackPlan plan;
        double origin_sec = 0;               // project time of plan time 0
        double start_sec = 0, end_sec = 0;   // phrase extent on the project timeline
        uint64_t hash = 0;                   // changes whenever anything that affects the audio changes
        std::vector<Diagnostic> diagnostics; // skipped notes, unpronounceable lyrics, range warnings
        int skipped_notes = 0;
    };
    // Lead-in before the first note so that consonants that start before the vowel onset are not cut at the plan origin.
    constexpr double kPhraseLeadSec = 0.15;

    BuiltPlan build_phrase_plan(const Project& p, const Track& t, const Voicebank& vb, const PronunciationEngine& pron, const PhraseSpan& span);

    // Whole-track plan (all notes, project time origin 0). Used for diagnostics and tests.
    BuiltPlan build_track_plan(const Project& p, const Track& t, const Voicebank& vb, const PronunciationEngine& pron);

    // Everything that could stop a project from rendering as the user expects, before rendering: missing voicebanks, notes outside the
    // voice's range, unpronounceable lyrics, kanji, overlaps. Uses the same plan code as the renderer.
    std::vector<Diagnostic> preflight(const Project& p, const std::map<std::string, Voicebank>& voicebanks, const PronunciationEngine& pron);
}
