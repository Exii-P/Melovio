// Discovery, installation, removal, versioning and project resolution of voicebanks.
#pragma once
#include "project.hpp"
#include "voicebank.hpp"
#include <deque>

namespace melovio {
    struct VoicebankResolution {
        enum class State { Found, FoundOtherVersion, FoundAtHint, Missing, Unusable };
        std::string id;
        State state = State::Missing;
        const Voicebank* voicebank = nullptr;
        std::string message;
        bool ok() const { return state == State::Found || state == State::FoundOtherVersion || state == State::FoundAtHint; }
    };

    class VoicebankLibrary {
    public:
        // Directory where imported voicebanks are stored (created on demand). Voicebanks live in "<user_dir>/<id>-<version>/".
        void set_user_dir(const fs::path& d) { user_dir_ = d; }
        const fs::path& user_dir() const { return user_dir_; }
        // Extra read-only locations scanned for voicebanks (the app's bundled folder, shared Wauvio folders, ...).
        void add_search_dir(const fs::path& d, bool bundled);
        void refresh();

        const std::deque<Voicebank>& all() const { return banks_; }
        // Best match: exact version when given and present, otherwise the highest usable version. Never returns an unusable voicebank
        // when a usable one exists. nullptr if nothing with that id is installed.
        const Voicebank* find(const std::string& id, const std::string& version = "") const;
        std::vector<const Voicebank*> versions_of(const std::string& id) const;   // newest first

        // Imports a voicebank directory or a .mvb package. Refuses packages with errors (the message lists them). Warnings are returned.
        Result<const Voicebank*> install(const fs::path& source, bool replace_existing = false, std::vector<Diagnostic>* warnings = nullptr);
        // Removes an installed (non-bundled) voicebank version from the user directory.
        Status remove(const std::string& id, const std::string& version);
        // Packs a voicebank into a .mvb (zip) file.
        Status export_package(const Voicebank& vb, const fs::path& out_file) const;

        // Resolves every voicebank a project needs: library by id/version, then the project's relative path hint.
        std::vector<VoicebankResolution> resolve(const Project& p, const fs::path& project_dir);
        // User picked a folder for a missing voicebank. Loads it, checks the id matches, and records a *relative* hint in the project.
        Status locate(Project& p, const std::string& id, const fs::path& chosen_dir, const fs::path& project_dir);

    private:
        struct Dir { fs::path path; bool bundled; };
        void scan(const Dir& d);
        fs::path user_dir_;
        std::vector<Dir> dirs_;
        std::deque<Voicebank> banks_;        // stable addresses until refresh()
        std::deque<Voicebank> external_;     // loaded from project path hints
    };

    // Default per-user directories (XDG on Linux, %APPDATA% on Windows, ~/Library/Application Support on macOS).
    fs::path default_user_data_dir();
    fs::path default_config_dir();

    // Zip helpers (also used by tests). Extraction rejects absolute paths, "..", and oversized archives.
    Status unzip_package(const fs::path& zip_file, const fs::path& dest_dir);
    Status zip_directory(const fs::path& dir, const fs::path& zip_file);
}

namespace melovio {
    struct AppConfig;
    // Standard setup used by the GUI and CLI: bundled voicebanks next to the executable (./voicebanks, ../share/melovio/voicebanks),
    // the MELOVIO_VOICEBANKS environment variable (path list), extra dirs from the config, and the per-user directory.
    void setup_default_library(VoicebankLibrary& lib, const fs::path& exe_dir, const AppConfig* cfg = nullptr);
    // Snapshot of the voicebanks a project uses, keyed by id, for the Renderer/Player.
    std::map<std::string, Voicebank> snapshot_for_project(const std::vector<VoicebankResolution>& res);
}
