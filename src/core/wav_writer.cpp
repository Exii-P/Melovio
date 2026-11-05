#include "wav_writer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace melovio {
    bool valid_wav_format(const WavFormat& f, std::string* why) {
        auto no = [&](const char* m) { if (why) *why = m; return false; };
        if (f.sample_rate < 8000 || f.sample_rate > 192000) return no("sample rate must be between 8000 and 192000 Hz");
        if (f.channels != 1 && f.channels != 2) return no("channels must be 1 or 2");
        if (f.bit_depth != 16 && f.bit_depth != 24 && f.bit_depth != 32) return no("bit depth must be 16, 24 or 32 (float)");
        return true;
    }

    static void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(static_cast<uint8_t>(v & 255)); o.push_back(static_cast<uint8_t>(v >> 8)); }
    static void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((v >> (8 * i)) & 255); }
    static void puts(std::vector<uint8_t>& o, const char* s) { o.insert(o.end(), s, s + 4); }

    Result<std::vector<uint8_t>> encode_wav(const std::vector<float>& in, const WavFormat& f) {
        using R = Result<std::vector<uint8_t>>;
        std::string why; if (!valid_wav_format(f, &why)) return R::fail(why);
        if (in.size() % static_cast<size_t>(f.channels) != 0) return R::fail("sample count is not a multiple of the channel count");
        const uint32_t bytes_per = static_cast<uint32_t>(f.bit_depth / 8);
        const uint64_t data_bytes = static_cast<uint64_t>(in.size()) * bytes_per;
        if (data_bytes > 0xFFFFFFFFull - 64) return R::fail("audio is too long for a WAV file (4 GiB limit)");
        std::vector<uint8_t> o; o.reserve(static_cast<size_t>(data_bytes) + 64);
        const bool fl = f.bit_depth == 32;
        puts(o, "RIFF"); put32(o, static_cast<uint32_t>(36 + data_bytes)); puts(o, "WAVE");
        puts(o, "fmt "); put32(o, 16); put16(o, fl ? 3 : 1); put16(o, static_cast<uint16_t>(f.channels));
        put32(o, static_cast<uint32_t>(f.sample_rate)); put32(o, static_cast<uint32_t>(f.sample_rate) * f.channels * bytes_per);
        put16(o, static_cast<uint16_t>(f.channels * bytes_per)); put16(o, static_cast<uint16_t>(f.bit_depth));
        puts(o, "data"); put32(o, static_cast<uint32_t>(data_bytes));
        uint32_t rng = 0x12345678u;
        auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return static_cast<float>(rng & 0xFFFFFF) / 16777216.f; };
        for (float x : in) {
            if (!std::isfinite(x)) x = 0.f;
            x = std::clamp(x, -1.f, 1.f);
            if (fl) { uint32_t b; std::memcpy(&b, &x, 4); put32(o, b); continue; }
            const double scale = f.bit_depth == 16 ? 32767.0 : 8388607.0;
            double v = static_cast<double>(x) * scale;
            if (f.dither) v += static_cast<double>(rnd() - rnd());     // TPDF, +-1 LSB
            long q = std::lround(v);
            q = std::clamp<long>(q, f.bit_depth == 16 ? -32768 : -8388608, static_cast<long>(scale));
            if (f.bit_depth == 16) put16(o, static_cast<uint16_t>(static_cast<int16_t>(q)));
            else { const uint32_t u = static_cast<uint32_t>(q) & 0xFFFFFFu; o.push_back(u & 255); o.push_back((u >> 8) & 255); o.push_back((u >> 16) & 255); }
        }
        return R::success(std::move(o));
    }

    Status write_wav_file(const fs::path& path, const std::vector<float>& in, const WavFormat& f) {
        auto bytes = encode_wav(in, f);
        if (!bytes) return Status::fail(bytes.error);
        std::error_code ec; if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
        const fs::path tmp = path.string() + ".part";
        { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); if (!o) return Status::fail("cannot write " + path_to_utf8(path));
            o.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size())); o.flush(); if (!o) return Status::fail("write failed (disk full?)"); }
        fs::rename(tmp, path, ec);
        if (ec) { fs::remove(tmp, ec); return Status::fail("cannot finish " + path_to_utf8(path)); }
        return Status::success();
    }

    Result<WavData> decode_wav(const std::vector<uint8_t>& b) {
        using R = Result<WavData>;
        if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return R::fail("not a WAV file");
        auto u16 = [&](size_t p) { return static_cast<uint16_t>(b[p] | (b[p + 1] << 8)); };
        auto u32 = [&](size_t p) { return static_cast<uint32_t>(b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (static_cast<uint32_t>(b[p + 3]) << 24)); };
        WavData w; size_t pos = 12; uint16_t tag = 0; bool have_fmt = false;
        while (pos + 8 <= b.size()) {
            const uint32_t sz = u32(pos + 4);
            if (!std::memcmp(b.data() + pos, "fmt ", 4) && pos + 8 + 16 <= b.size()) {
                tag = u16(pos + 8); w.format.channels = u16(pos + 10); w.format.sample_rate = static_cast<int>(u32(pos + 12)); w.format.bit_depth = u16(pos + 22); have_fmt = true;
            } else if (!std::memcmp(b.data() + pos, "data", 4)) {
                if (!have_fmt) return R::fail("data chunk before fmt chunk");
                const size_t n = std::min<size_t>(sz, b.size() - pos - 8); const size_t bp = static_cast<size_t>(w.format.bit_depth / 8);
                if (bp == 0 || (tag != 1 && tag != 3)) return R::fail("unsupported WAV encoding");
                const uint8_t* d = b.data() + pos + 8;
                for (size_t i = 0; i + bp <= n; i += bp) {
                    if (tag == 3 && bp == 4) { float x; std::memcpy(&x, d + i, 4); w.samples.push_back(x); }
                    else if (bp == 2) w.samples.push_back(static_cast<int16_t>(d[i] | (d[i + 1] << 8)) / 32768.f);
                    else if (bp == 3) { int32_t v = d[i] | (d[i + 1] << 8) | (d[i + 2] << 16); if (v & 0x800000) v |= ~0xFFFFFF; w.samples.push_back(static_cast<float>(v) / 8388608.f); }
                    else return R::fail("unsupported bit depth");
                }
                return R::success(std::move(w));
            }
            pos += 8 + sz + (sz & 1);
        }
        return R::fail("no data chunk");
    }

    Result<WavData> read_wav_file(const fs::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return Result<WavData>::fail("cannot open " + path_to_utf8(path));
        std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return decode_wav(b);
    }
}
