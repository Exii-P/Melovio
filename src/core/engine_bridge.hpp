// The ONLY place (together with vb_validate.cpp / pronunciation.cpp) that includes Wauvio-Vocal. Everything above this layer is
// engine-free, which keeps compile times low and makes the engine swappable behind this narrow interface.
#pragma once
#include "util.hpp"
#include <map>
#include <string>
#include <vector>

namespace melovio {
    struct VowelParams { float f1 = 0, f2 = 0, f3 = 0, b1 = 0, b2 = 0, b3 = 0, gain = 1, f1_track = 0, f2_track = 0; };

    struct ProfileParams {
        std::string id, display, language, notes, model_name;
        float natural_amount = 0.6f;
        int range_low = 52, range_high = 88, hard_low = 40, hard_high = 96;
        VowelParams vowel[5];                              // a i u e o
        std::map<std::string, float> model;                // engine "model <name> <value>" keys (f4, singer_formant_hz, rd_base, vib_depth_st ...)
        std::map<std::string, float> tune;                 // engine "tune <name> <value>" keys
        std::map<std::string, float> style;                // engine "style <name> <value>" keys (the character's default look)
        bool parse_merge_long_vowels = true, parse_cute = false;   // lyric parsing defaults of this character
        std::vector<std::string> personas;                 // names only (personas are preserved verbatim when a profile is rewritten)
    };

    // Loads a .voice file with the engine's own strict parser (unknown keys and malformed lines are errors, with line numbers).
    Result<ProfileParams> read_profile(const fs::path& voice_file);
    // Rewrites `voice_file` from `params`, starting from the file's current contents so personas and unlisted values survive.
    Status write_profile(const ProfileParams& params, const fs::path& voice_file);
    // Names of all built-in (compiled-in) engine voices: yune, reiha, koharu, itsuki.
    std::vector<std::string> engine_builtin_voice_ids();
    // Writes a compiled-in voice to a .voice file (used to build the bundled voicebanks reproducibly).
    Status export_builtin_voice(const std::string& id, const fs::path& out);

    struct PlanNote {
        double start = 0, dur = 0;                // seconds, relative to the plan origin
        int midi = 60;
        float velocity = 0.75f;
        std::string engine_lyric;                 // what the engine parses (kana / romaji / "{phonemes}")
        std::map<std::string, float> style;       // per-note Style overrides
        int source_index = -1;                    // index in the project track (for diagnostics)
    };
    struct PlanLane { std::string name; std::vector<std::pair<double, double>> curve; };   // (seconds, value)

    struct TrackPlan {
        std::string profile_path;                 // .voice file
        std::string persona; float persona_amount = 1.f;
        std::map<std::string, float> style;       // track-wide Style overrides
        float natural = 0.f; int natural_take = 1; bool auto_expression = false;
        std::vector<PlanNote> notes;              // sorted by start
        std::vector<PlanLane> lanes;
        uint32_t seed = 0;                        // 0 = engine default
    };

    struct EngineInfo { std::string name, version, kind; };
    EngineInfo engine_info();

    // Renders mono float samples at `sample_rate`. Throws nothing; returns an error string on failure (bad profile, engine exception).
    Result<std::vector<float>> render_plan(const TrackPlan& plan, int sample_rate);
    // Engine diagnostics for a plan (range problems, too many morae for a note's length, ...).
    std::vector<Diagnostic> diagnose_plan(const TrackPlan& plan);
}
