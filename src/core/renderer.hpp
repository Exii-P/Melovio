// Rendering: project -> audio, using Wauvio-Vocal. Phrase-level caching makes re-renders after small edits cheap;
// phrases are synthesised in parallel; progress and cancellation are supported.
#pragma once
#include "project.hpp"
#include "pronunciation.hpp"
#include "synth_plan.hpp"
#include "voicebank.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace melovio {
    using ProgressFn = std::function<void(double fraction, const std::string& message)>;
    struct CancelToken {
        std::atomic<bool> flag{ false };
        void cancel() { flag.store(true); }
        void reset() { flag.store(false); }
        bool cancelled() const { return flag.load(); }
    };

    struct StereoAudio {
        int sample_rate = 44100;
        double start_sec = 0;                // project time of the first frame
        std::vector<float> left, right;
        size_t frames() const { return left.size(); }
        double duration_sec() const { return sample_rate ? static_cast<double>(frames()) / sample_rate : 0; }
        // 1 = mono sum, 2 = interleaved L/R
        std::vector<float> interleaved(int channels) const;
        double peak() const;
    };

    class PhraseCache {
    public:
        explicit PhraseCache(size_t max_bytes = 256u << 20) : max_bytes_(max_bytes) {}
        std::shared_ptr<const std::vector<float>> get(uint64_t key);
        void put(uint64_t key, std::shared_ptr<const std::vector<float>> v);
        size_t bytes() const { std::lock_guard<std::mutex> l(m_); return bytes_; }
        size_t entries() const { std::lock_guard<std::mutex> l(m_); return map_.size(); }
        void clear() { std::lock_guard<std::mutex> l(m_); map_.clear(); order_.clear(); bytes_ = 0; }
        uint64_t hits() const { return hits_.load(); }
        uint64_t misses() const { return misses_.load(); }
    private:
        mutable std::mutex m_;
        std::unordered_map<uint64_t, std::shared_ptr<const std::vector<float>>> map_;
        std::vector<uint64_t> order_;       // insertion order, oldest first
        size_t bytes_ = 0, max_bytes_;
        std::atomic<uint64_t> hits_{ 0 }, misses_{ 0 };
    };

    struct RenderOptions {
        int sample_rate = 44100;
        double t0 = 0;                       // region start (project seconds)
        double t1 = -1;                      // region end; < 0 = end of the project's notes + tail
        double tail_sec = 0.6;               // appended after the last note when t1 < 0 (release / reverb-free tail)
        bool apply_mute_solo = true;
        int threads = 0;                     // 0 = hardware concurrency
    };

    class Renderer {
    public:
        // `banks` is a snapshot of the resolved voicebanks keyed by id (so rendering is independent of library refreshes).
        Renderer(std::map<std::string, Voicebank> banks, std::shared_ptr<PhraseCache> cache = std::make_shared<PhraseCache>(),
                 std::shared_ptr<const PronunciationEngine> pron = std::make_shared<PronunciationEngine>());

        // One track, mono, length = (t1 - t0) * sample_rate. Skipped notes etc. are reported through `diags`.
        Result<std::vector<float>> render_track(const Project& p, const Track& t, const RenderOptions& o, const ProgressFn& progress = {},
                                                const CancelToken* cancel = nullptr, std::vector<Diagnostic>* diags = nullptr) const;
        // All audible tracks mixed to stereo with per-track gain and balance.
        Result<StereoAudio> render_mix(const Project& p, const RenderOptions& o, const ProgressFn& progress = {},
                                       const CancelToken* cancel = nullptr, std::vector<Diagnostic>* diags = nullptr) const;
        // Project end in seconds including the tail (what render_mix uses when t1 < 0).
        static double project_end_sec(const Project& p, double tail_sec);

        PhraseCache& cache() const { return *cache_; }

    private:
        std::map<std::string, Voicebank> banks_;
        std::shared_ptr<PhraseCache> cache_;
        std::shared_ptr<const PronunciationEngine> pron_;
    };
}
