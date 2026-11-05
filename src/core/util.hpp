// Small shared helpers: status/result type, paths, hashing, time stamps.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace melovio {
    namespace fs = std::filesystem;

    // Error reporting without exceptions across module boundaries.
    struct Status {
        bool ok = true;
        std::string message;
        static Status success() { return {}; }
        static Status fail(std::string m) { Status s; s.ok = false; s.message = std::move(m); return s; }
        explicit operator bool() const { return ok; }
    };

    template <class T>
    struct Result {
        std::optional<T> value;
        std::string error;
        bool ok() const { return value.has_value(); }
        explicit operator bool() const { return ok(); }
        T& operator*() { return *value; }
        const T& operator*() const { return *value; }
        T* operator->() { return &*value; }
        const T* operator->() const { return &*value; }
        static Result success(T v) { Result r; r.value = std::move(v); return r; }
        static Result fail(std::string e) { Result r; r.error = std::move(e); return r; }
    };

    enum class Severity { Info, Warning, Error };
    struct Diagnostic {
        Severity severity = Severity::Info;
        std::string code;     // stable machine-readable id, e.g. "voicebank.missing"
        std::string message;  // human readable
    };
    inline bool has_errors(const std::vector<Diagnostic>& d) {
        for (auto& x : d) if (x.severity == Severity::Error) return true;
        return false;
    }

    std::string read_text_file(const fs::path& p, std::string* err = nullptr);
    // Writes to "<p>.tmp" then renames over p, so a crash never leaves a half-written file.
    Status write_text_file_atomic(const fs::path& p, const std::string& content);

    std::string iso_utc_now();
    std::string make_id(const std::string& prefix);              // short unique id, e.g. "trk_3fa9c2"
    uint64_t fnv1a64(const void* data, size_t n, uint64_t seed = 1469598103934665603ull);
    inline uint64_t fnv1a64(const std::string& s, uint64_t seed = 1469598103934665603ull) { return fnv1a64(s.data(), s.size(), seed); }
    std::string to_hex(uint64_t v);

    // Path helpers (all UTF-8 aware via std::filesystem::u8path).
    fs::path path_from_utf8(const std::string& s);
    std::string path_to_utf8(const fs::path& p);
    // "a/b/c" relative to base directory, with forward slashes; nullopt when no relative form exists (different root).
    std::optional<std::string> relative_utf8(const fs::path& target, const fs::path& base_dir);
    bool is_absolute_utf8(const std::string& p);

    // "1.2.3" semantic-ish version compare. Returns <0, 0, >0. Missing components are 0.
    int compare_versions(const std::string& a, const std::string& b);
    bool valid_version(const std::string& v);
}
