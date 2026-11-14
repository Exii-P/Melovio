// Melovio project file (.mvp): JSON, versioned. See README.md.
#pragma once
#include "project.hpp"

namespace melovio {
    constexpr int kProjectFormatVersion = 1;
    constexpr const char* kProjectFormatId = "melovio-project";
    constexpr const char* kProjectExtension = ".mvp";

    // Serialises to pretty JSON. Voicebank path hints must be relative; absolute ones are converted relative to
    // `project_dir` when possible and dropped otherwise (a warning is appended to `warnings`).
    std::string project_to_json(const Project& p, const fs::path& project_dir = {}, std::vector<Diagnostic>* warnings = nullptr);

    // Parses a project. Errors (bad JSON, wrong format id, newer format version, wrong types) return a message that names the
    // offending field. Unknown keys are ignored (forward compatibility); unsupported parameters are kept and reported as warnings
    // by validate_project().
    Result<Project> project_from_json(const std::string& text, std::vector<Diagnostic>* warnings = nullptr);

    Status save_project(Project& p, const fs::path& path, std::vector<Diagnostic>* warnings = nullptr);   // updates meta.modified, atomic write
    Result<Project> load_project(const fs::path& path, std::vector<Diagnostic>* warnings = nullptr);

    // Autosaves live in a dedicated directory (never next to the user's file, never overwriting it). Each autosave records which
    // project it belongs to. After a clean save/close the autosave is discarded; if one is still there at start-up the app offers recovery.
    class AutosaveStore {
    public:
        explicit AutosaveStore(fs::path dir) : dir_(std::move(dir)) {}
        // `project_path` may be empty for a never-saved project (key "untitled").
        Status write(const Project& p, const fs::path& project_path) const;
        struct Candidate { fs::path autosave_file; std::string original_path; std::string modified; };
        // All autosaves found (crashed sessions), newest first. A candidate whose original file is newer than the autosave is skipped.
        std::vector<Candidate> find_recoverable() const;
        Result<Project> recover(const Candidate& c, std::vector<Diagnostic>* warnings = nullptr) const;
        void discard(const fs::path& project_path) const;
        void discard(const Candidate& c) const;
        fs::path file_for(const fs::path& project_path) const;
    private:
        fs::path dir_;
    };
}
