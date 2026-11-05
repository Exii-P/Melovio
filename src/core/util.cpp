#include "util.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

namespace melovio {
    std::string read_text_file(const fs::path& p, std::string* err) {
        std::ifstream f(p, std::ios::binary);
        if (!f) { if (err) *err = "cannot open " + path_to_utf8(p); return {}; }
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
    }

    Status write_text_file_atomic(const fs::path& p, const std::string& content) {
        std::error_code ec;
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        fs::path tmp = p; tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return Status::fail("cannot write " + path_to_utf8(tmp));
            f.write(content.data(), static_cast<std::streamsize>(content.size()));
            f.flush();
            if (!f) return Status::fail("write failed for " + path_to_utf8(tmp));
        }
        fs::rename(tmp, p, ec);
        if (ec) { fs::remove(tmp, ec); return Status::fail("cannot replace " + path_to_utf8(p) + ": " + ec.message()); }
        return Status::success();
    }

    std::string iso_utc_now() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
        return b;
    }

    std::string make_id(const std::string& prefix) {
        static std::atomic<uint64_t> counter{0};
        static const uint64_t salt = std::random_device{}() ^ static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        uint64_t v = fnv1a64(&salt, sizeof salt, counter.fetch_add(1) * 0x9E3779B97F4A7C15ull + 1);
        return prefix + "_" + to_hex(v).substr(0, 8);
    }

    uint64_t fnv1a64(const void* data, size_t n, uint64_t h) {
        auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    std::string to_hex(uint64_t v) {
        char b[17]; std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v)); return b;
    }

    fs::path path_from_utf8(const std::string& s) {
#if defined(__cpp_char8_t)
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
#else
        return fs::u8path(s);
#endif
    }
    std::string path_to_utf8(const fs::path& p) {
#if defined(__cpp_char8_t)
        auto u = p.u8string(); return std::string(reinterpret_cast<const char*>(u.data()), u.size());
#else
        return p.u8string();
#endif
    }

    std::optional<std::string> relative_utf8(const fs::path& target, const fs::path& base_dir) {
        std::error_code ec;
        fs::path t = fs::weakly_canonical(target, ec); if (ec) t = fs::absolute(target);
        fs::path b = fs::weakly_canonical(base_dir, ec); if (ec) b = fs::absolute(base_dir);
        if (t.root_name() != b.root_name()) return std::nullopt;
        fs::path r = t.lexically_relative(b);
        if (r.empty()) return std::nullopt;
        return path_to_utf8(r.generic_string().empty() ? r : fs::path(r.generic_string()));
    }

    bool is_absolute_utf8(const std::string& p) { return path_from_utf8(p).is_absolute() || (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\')); }

    static std::vector<long> split_ver(const std::string& v) {
        std::vector<long> o; std::string cur;
        for (char c : v + ".") { if (c == '.') { o.push_back(cur.empty() ? 0 : std::atol(cur.c_str())); cur.clear(); } else if (c >= '0' && c <= '9') cur += c; else break; }
        return o;
    }
    int compare_versions(const std::string& a, const std::string& b) {
        auto x = split_ver(a), y = split_ver(b);
        size_t n = std::max(x.size(), y.size()); x.resize(n, 0); y.resize(n, 0);
        for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
        return 0;
    }
    bool valid_version(const std::string& v) {
        if (v.empty()) return false;
        int dots = 0; bool digit = false;
        for (char c : v) { if (c == '.') { if (!digit) return false; ++dots; digit = false; } else if (c >= '0' && c <= '9') digit = true; else return false; }
        return digit && dots >= 1 && dots <= 3;
    }
}
