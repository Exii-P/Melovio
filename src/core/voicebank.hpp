// Melovio voicebank package: a directory (or .mvb zip) containing voicebank.json + the synthesis profile. See README.md.
#pragma once
#include "engine_bridge.hpp"
#include "util.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace melovio {
    constexpr int kVoicebankFormatVersion = 1;
    constexpr const char* kVoicebankFormatId = "melovio-voicebank";
    constexpr const char* kVoicebankManifestName = "voicebank.json";
    constexpr const char* kVoicebankPackageExt = ".mvb";

    struct VoicebankManifest {
        std::string id;                      // [a-z0-9_-]+, unique key referenced by projects
        std::string version = "1.0.0";
        std::string name;                    // display name (Latin)
        std::string native_name;             // e.g. 夢音
        std::string author, license, description, tagline, homepage;
        std::vector<std::string> languages = { "ja" };
        std::vector<std::string> tags;
        std::string character_notes;
        // Synthesis
        std::string engine = "wauvio-vocal";
        std::string engine_version = "v2";
        std::string kind = "parametric-formant";   // "parametric-formant" is the only kind the current engine can render
        std::string profile = "voice.voice";       // relative to the package root
        // Phonetic data
        struct Recording { std::string phoneme, file; };
        std::vector<Recording> recordings;         // sample references; the current engine cannot play them (see validation)
        std::map<std::string, std::string> phonetic_mappings;   // lyric text -> phoneme override, applied before synthesis
        std::string preview_lyric = "さくらさくら";
        // Compatibility
        std::string min_melovio = "0.1.0";
        // Explicit statement of what is and is not in the package (shown in the UI, never hidden)
        std::string completeness;
    };

    struct Voicebank {
        VoicebankManifest manifest;
        fs::path root;                       // package directory
        bool bundled = false;                // shipped with the app (read-only; cannot be removed from the UI)
        fs::path profile_path() const { return root / path_from_utf8(manifest.profile); }
        ProfileParams profile;               // read at load time; valid when profile_ok
        bool profile_ok = false;
        std::string profile_error;
        std::vector<Diagnostic> diagnostics; // from load-time validation (never empty-silent: errors mean not usable)
        bool usable() const { return profile_ok && !has_errors(diagnostics); }
    };

    Result<VoicebankManifest> manifest_from_json(const std::string& text);
    std::string manifest_to_json(const VoicebankManifest& m);
    // Parses voicebank.json in `dir`, reads and validates the profile. The Voicebank is returned even when it has errors
    // (so the UI can show *why* it is unusable) as long as the manifest itself parses.
    Result<Voicebank> load_voicebank(const fs::path& dir, bool bundled = false);
    // Static checks: required fields, id syntax, engine/kind support, profile present and parseable, referenced files exist, compatibility.
    std::vector<Diagnostic> validate_voicebank(const Voicebank& vb);

    struct PhonemeCheck { std::string unit; std::string phonemes; bool ok = false; double peak = 0, rms = 0; std::string problem; };
    struct CompletenessReport {
        std::vector<PhonemeCheck> checks;
        size_t total = 0, passed = 0;
        std::vector<Diagnostic> diagnostics;
        bool complete() const { return total > 0 && passed == total && !has_errors(diagnostics); }
    };
    // Synthesises every standard Japanese kana unit (basic, voiced, yo-on, ん っ ー, ...) through the real engine at a mid-range pitch,
    // then checks the result is finite, non-silent and not clipping. Also renders the vowels at the range limits.
    CompletenessReport check_completeness(const Voicebank& vb, int sample_rate = 22050);
}
