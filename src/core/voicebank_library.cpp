#include "voicebank_library.hpp"
#include <miniz.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace melovio {
    fs::path default_user_data_dir() {
#if defined(_WIN32)
        if (const char* a = std::getenv("APPDATA")) return fs::path(a) / "Melovio";
#elif defined(__APPLE__)
        if (const char* h = std::getenv("HOME")) return fs::path(h) / "Library/Application Support/Melovio";
#else
        if (const char* x = std::getenv("XDG_DATA_HOME")) if (*x) return fs::path(x) / "melovio";
        if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/melovio";
#endif
        return fs::temp_directory_path() / "melovio-data";
    }
    fs::path default_config_dir() {
#if defined(_WIN32)
        if (const char* a = std::getenv("APPDATA")) return fs::path(a) / "Melovio";
#elif defined(__APPLE__)
        if (const char* h = std::getenv("HOME")) return fs::path(h) / "Library/Application Support/Melovio";
#else
        if (const char* x = std::getenv("XDG_CONFIG_HOME")) if (*x) return fs::path(x) / "melovio";
        if (const char* h = std::getenv("HOME")) return fs::path(h) / ".config/melovio";
#endif
        return fs::temp_directory_path() / "melovio-config";
    }

    static bool safe_entry_name(const std::string& n) {
        if (n.empty() || n[0] == '/' || n[0] == '\\' || (n.size() > 1 && n[1] == ':')) return false;
        if (n.find('\\') != std::string::npos) return false;
        size_t i = 0;
        while (i <= n.size()) {
            size_t j = n.find('/', i); if (j == std::string::npos) j = n.size();
            if (n.substr(i, j - i) == "..") return false;
            i = j + 1;
        }
        return true;
    }

    Status unzip_package(const fs::path& zip_file, const fs::path& dest) {
        mz_zip_archive z{};
        if (!mz_zip_reader_init_file(&z, path_to_utf8(zip_file).c_str(), 0)) return Status::fail("cannot open package (not a valid zip): " + path_to_utf8(zip_file));
        struct Closer { mz_zip_archive* z; ~Closer() { mz_zip_reader_end(z); } } closer{ &z };
        const mz_uint n = mz_zip_reader_get_num_files(&z);
        if (n == 0 || n > 20000) return Status::fail("package is empty or has too many files");
        uint64_t total = 0;
        std::error_code ec; fs::create_directories(dest, ec);
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&z, i, &st)) return Status::fail("corrupt zip entry");
            const std::string name = st.m_filename;
            if (!safe_entry_name(name)) return Status::fail("package contains an unsafe path: " + name);
            total += st.m_uncomp_size;
            if (total > (1ull << 30)) return Status::fail("package is larger than 1 GiB uncompressed");
            const fs::path out = dest / path_from_utf8(name);
            if (mz_zip_reader_is_file_a_directory(&z, i)) { fs::create_directories(out, ec); continue; }
            fs::create_directories(out.parent_path(), ec);
            if (!mz_zip_reader_extract_to_file(&z, i, path_to_utf8(out).c_str(), 0)) return Status::fail("cannot extract " + name);
        }
        return Status::success();
    }

    Status zip_directory(const fs::path& dir, const fs::path& zip_file) {
        std::error_code ec; fs::remove(zip_file, ec);
        if (zip_file.has_parent_path()) fs::create_directories(zip_file.parent_path(), ec);
        mz_zip_archive z{};
        if (!mz_zip_writer_init_file(&z, path_to_utf8(zip_file).c_str(), 0)) return Status::fail("cannot create " + path_to_utf8(zip_file));
        bool ok = true; std::string err;
        std::vector<fs::path> files;
        for (auto& e : fs::recursive_directory_iterator(dir, ec)) if (e.is_regular_file()) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            const std::string name = path_to_utf8(f.lexically_relative(dir)).empty() ? "" : fs::path(f.lexically_relative(dir)).generic_string();
            if (!mz_zip_writer_add_file(&z, name.c_str(), path_to_utf8(f).c_str(), nullptr, 0, MZ_DEFAULT_LEVEL)) { ok = false; err = "cannot add " + name; break; }
        }
        if (ok) ok = mz_zip_writer_finalize_archive(&z) != 0;
        mz_zip_writer_end(&z);
        if (!ok) { fs::remove(zip_file, ec); return Status::fail(err.empty() ? "zip finalize failed" : err); }
        return Status::success();
    }

    void VoicebankLibrary::add_search_dir(const fs::path& d, bool bundled) {
        for (auto& x : dirs_) if (x.path == d) return;
        dirs_.push_back({ d, bundled });
    }

    void VoicebankLibrary::scan(const Dir& d) {
        std::error_code ec;
        if (!fs::is_directory(d.path, ec)) return;
        std::vector<fs::path> subs;
        for (auto& e : fs::directory_iterator(d.path, ec)) if (e.is_directory(ec) && e.path().filename().string()[0] != '.') subs.push_back(e.path());
        std::sort(subs.begin(), subs.end());
        for (auto& s : subs) {
            auto r = load_voicebank(s, d.bundled);
            if (r) banks_.push_back(std::move(*r));
            else if (fs::exists(s / kVoicebankManifestName, ec)) {
                // A package that has a manifest but cannot be parsed is kept visible as an unusable entry instead of vanishing.
                Voicebank vb; vb.root = s; vb.bundled = d.bundled; vb.manifest.id = path_to_utf8(s.filename()); vb.manifest.name = vb.manifest.id; vb.manifest.version = "0";
                vb.diagnostics.push_back({ Severity::Error, "vb.manifest_invalid", r.error });
                banks_.push_back(std::move(vb));
            }
        }
    }

    void VoicebankLibrary::refresh() {
        banks_.clear();
        for (auto& d : dirs_) scan(d);
        if (!user_dir_.empty()) scan({ user_dir_, false });
    }

    std::vector<const Voicebank*> VoicebankLibrary::versions_of(const std::string& id) const {
        std::vector<const Voicebank*> v;
        for (auto& b : banks_) if (b.manifest.id == id) v.push_back(&b);
        std::stable_sort(v.begin(), v.end(), [](const Voicebank* a, const Voicebank* b) {
            if (a->usable() != b->usable()) return a->usable();
            const int c = compare_versions(a->manifest.version, b->manifest.version);
            if (c != 0) return c > 0;
            return !a->bundled && b->bundled;       // user copy overrides a bundled one of the same version
        });
        return v;
    }

    const Voicebank* VoicebankLibrary::find(const std::string& id, const std::string& version) const {
        auto v = versions_of(id);
        if (v.empty()) return nullptr;
        if (!version.empty()) for (auto* b : v) if (b->usable() && compare_versions(b->manifest.version, version) == 0) return b;
        return v.front();
    }

    Result<const Voicebank*> VoicebankLibrary::install(const fs::path& source, bool replace, std::vector<Diagnostic>* warnings) {
        using R = Result<const Voicebank*>;
        if (user_dir_.empty()) return R::fail("no user voicebank directory configured");
        std::error_code ec;
        if (!fs::exists(source, ec)) return R::fail("not found: " + path_to_utf8(source));
        fs::create_directories(user_dir_, ec);
        const fs::path staging = user_dir_ / (".staging-" + make_id("s"));
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove_all(p, e); } } cleanup{ staging };
        fs::create_directories(staging, ec);
        if (fs::is_directory(source, ec)) {
            fs::copy(source, staging, fs::copy_options::recursive, ec);
            if (ec) return R::fail("cannot copy voicebank: " + ec.message());
        } else {
            Status st = unzip_package(source, staging);
            if (!st) return R::fail(st.message);
        }
        // allow a single top-level folder inside the package
        fs::path root = staging;
        if (!fs::exists(root / kVoicebankManifestName, ec)) {
            std::vector<fs::path> kids; for (auto& e : fs::directory_iterator(staging, ec)) kids.push_back(e.path());
            if (kids.size() == 1 && fs::is_directory(kids[0], ec) && fs::exists(kids[0] / kVoicebankManifestName, ec)) root = kids[0];
        }
        auto loaded = load_voicebank(root, false);
        if (!loaded) return R::fail(loaded.error);
        if (!loaded->usable()) {
            std::string msg = "voicebank cannot be installed:";
            for (auto& d : loaded->diagnostics) if (d.severity == Severity::Error) msg += "\n  - " + d.message;
            return R::fail(msg);
        }
        if (warnings) for (auto& d : loaded->diagnostics) if (d.severity == Severity::Warning) warnings->push_back(d);
        const fs::path dest = user_dir_ / path_from_utf8(loaded->manifest.id + "-" + loaded->manifest.version);
        if (fs::exists(dest, ec)) {
            if (!replace) return R::fail("voicebank '" + loaded->manifest.id + "' version " + loaded->manifest.version + " is already installed");
            fs::remove_all(dest, ec);
        }
        fs::rename(root, dest, ec);
        if (ec) { fs::copy(root, dest, fs::copy_options::recursive, ec); if (ec) return R::fail("cannot install: " + ec.message()); }
        refresh();
        for (auto& b : banks_) if (!b.bundled && b.root == dest) return R::success(&b);
        return R::fail("installed, but the voicebank was not found after refresh");
    }

    Status VoicebankLibrary::remove(const std::string& id, const std::string& version) {
        for (auto& b : banks_) {
            if (b.manifest.id != id || b.manifest.version != version) continue;
            if (b.bundled) return Status::fail("'" + id + "' is bundled with Melovio and cannot be removed");
            std::error_code ec;
            // Safety: only ever delete inside the user directory.
            const fs::path canon = fs::weakly_canonical(b.root, ec), user = fs::weakly_canonical(user_dir_, ec);
            if (canon.parent_path() != user) return Status::fail("refusing to delete outside the user voicebank folder");
            fs::remove_all(b.root, ec);
            if (ec) return Status::fail("cannot remove: " + ec.message());
            refresh();
            return Status::success();
        }
        return Status::fail("voicebank '" + id + "' version " + version + " is not installed");
    }

    Status VoicebankLibrary::export_package(const Voicebank& vb, const fs::path& out) const { return zip_directory(vb.root, out); }

    std::vector<VoicebankResolution> VoicebankLibrary::resolve(const Project& p, const fs::path& project_dir) {
        std::vector<std::string> ids;
        for (auto& t : p.tracks) if (!t.voicebank_id.empty() && std::find(ids.begin(), ids.end(), t.voicebank_id) == ids.end()) ids.push_back(t.voicebank_id);
        std::vector<VoicebankResolution> out;
        for (auto& id : ids) {
            VoicebankResolution r; r.id = id;
            const VoicebankRef* ref = p.find_voicebank_ref(id);
            const std::string want = ref ? ref->version : "";
            const Voicebank* vb = find(id, want);
            if (!vb) for (auto& e : external_) if (e.manifest.id == id && e.usable()) { vb = &e; break; }   // located manually this session
            if (vb && vb->usable()) {
                r.voicebank = vb;
                if (!want.empty() && compare_versions(vb->manifest.version, want) != 0) {
                    r.state = VoicebankResolution::State::FoundOtherVersion;
                    r.message = "project was made with version " + want + ", using installed " + vb->manifest.version;
                } else r.state = VoicebankResolution::State::Found;
                out.push_back(r); continue;
            }
            if (ref && !ref->path_hint.empty() && !project_dir.empty()) {
                const fs::path hp = project_dir / path_from_utf8(ref->path_hint);
                auto loaded = load_voicebank(hp, false);
                if (loaded && loaded->manifest.id == id) {
                    external_.push_back(std::move(*loaded));
                    r.voicebank = &external_.back();
                    r.state = r.voicebank->usable() ? VoicebankResolution::State::FoundAtHint : VoicebankResolution::State::Unusable;
                    r.message = r.voicebank->usable() ? "loaded from " + ref->path_hint : "found at " + ref->path_hint + " but it has errors";
                    if (!r.voicebank->usable()) r.voicebank = nullptr;
                    out.push_back(r); continue;
                }
            }
            if (vb) { r.state = VoicebankResolution::State::Unusable; r.message = "installed but has errors: " + (vb->diagnostics.empty() ? vb->profile_error : vb->diagnostics.front().message); }
            else { r.state = VoicebankResolution::State::Missing; r.message = "voicebank '" + id + "' is not installed" + (ref && !ref->name.empty() ? " (" + ref->name + ")" : ""); }
            out.push_back(r);
        }
        return out;
    }

    Status VoicebankLibrary::locate(Project& p, const std::string& id, const fs::path& chosen_dir, const fs::path& project_dir) {
        auto loaded = load_voicebank(chosen_dir, false);
        if (!loaded) return Status::fail(loaded.error);
        if (loaded->manifest.id != id) return Status::fail("that folder contains voicebank '" + loaded->manifest.id + "', not '" + id + "'");
        if (!loaded->usable()) return Status::fail("that voicebank has errors: " + (loaded->diagnostics.empty() ? loaded->profile_error : loaded->diagnostics.front().message));
        auto rel = project_dir.empty() ? std::nullopt : relative_utf8(chosen_dir, project_dir);
        p.ensure_voicebank_ref(id, loaded->manifest.version, loaded->manifest.name);
        if (rel) p.find_voicebank_ref(id)->path_hint = *rel;
        external_.push_back(std::move(*loaded));
        return Status::success();
    }
}

#include "config.hpp"
namespace melovio {
    void setup_default_library(VoicebankLibrary& lib, const fs::path& exe_dir, const AppConfig* cfg) {
        lib.add_search_dir(exe_dir / "voicebanks", true);
        lib.add_search_dir(exe_dir / ".." / "share" / "melovio" / "voicebanks", true);
        if (const char* env = std::getenv("MELOVIO_VOICEBANKS")) {
        #ifdef _WIN32
            const char sep = ';';
        #else
            const char sep = ':';
        #endif
            std::string s = env; size_t i = 0;
            while (i <= s.size()) { size_t j = s.find(sep, i); if (j == std::string::npos) j = s.size(); if (j > i) lib.add_search_dir(path_from_utf8(s.substr(i, j - i)), false); i = j + 1; }
        }
        if (cfg) for (auto& d : cfg->voicebank_dirs) lib.add_search_dir(path_from_utf8(d), false);
        lib.set_user_dir(default_user_data_dir() / "voicebanks");
        lib.refresh();
    }
    std::map<std::string, Voicebank> snapshot_for_project(const std::vector<VoicebankResolution>& res) {
        std::map<std::string, Voicebank> m;
        for (auto& r : res) if (r.ok() && r.voicebank) m[r.id] = *r.voicebank;
        return m;
    }
}
