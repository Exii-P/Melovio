// WAV writer/reader: 16/24-bit PCM (optional TPDF dither) and 32-bit float, mono or stereo. Wauvio's own writer only does 16-bit.
#pragma once
#include "util.hpp"
#include <vector>

namespace melovio {
    struct WavFormat {
        int sample_rate = 44100;
        int channels = 2;        // 1 or 2
        int bit_depth = 16;      // 16, 24 (PCM) or 32 (IEEE float)
        bool dither = true;      // TPDF dither, 16/24-bit only
    };
    bool valid_wav_format(const WavFormat& f, std::string* why = nullptr);

    // `interleaved` holds frames*channels floats in [-1,1] (values beyond are clamped).
    Result<std::vector<uint8_t>> encode_wav(const std::vector<float>& interleaved, const WavFormat& f);
    Status write_wav_file(const fs::path& path, const std::vector<float>& interleaved, const WavFormat& f);

    struct WavData { WavFormat format; std::vector<float> samples; size_t frames() const { return format.channels ? samples.size() / static_cast<size_t>(format.channels) : 0; } };
    Result<WavData> read_wav_file(const fs::path& path);
    Result<WavData> decode_wav(const std::vector<uint8_t>& bytes);
}
