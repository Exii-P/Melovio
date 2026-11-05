#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <thread>

namespace melovio {
    std::vector<float> StereoAudio::interleaved(int channels) const {
        std::vector<float> o;
        if (channels == 1) { o.resize(frames()); for (size_t i = 0; i < frames(); ++i) o[i] = 0.5f * (left[i] + right[i]); }
        else { o.resize(frames() * 2); for (size_t i = 0; i < frames(); ++i) { o[2 * i] = left[i]; o[2 * i + 1] = right[i]; } }
        return o;
    }
    double StereoAudio::peak() const {
        float p = 0; for (float x : left) p = std::max(p, std::fabs(x)); for (float x : right) p = std::max(p, std::fabs(x)); return p;
    }

    std::shared_ptr<const std::vector<float>> PhraseCache::get(uint64_t key) {
        std::lock_guard<std::mutex> l(m_);
        auto it = map_.find(key);
        if (it == map_.end()) { ++misses_; return nullptr; }
        ++hits_; return it->second;
    }
    void PhraseCache::put(uint64_t key, std::shared_ptr<const std::vector<float>> v) {
        std::lock_guard<std::mutex> l(m_);
        if (map_.count(key)) return;
        bytes_ += v->size() * sizeof(float); map_[key] = std::move(v); order_.push_back(key);
        while (bytes_ > max_bytes_ && order_.size() > 1) {
            auto it = map_.find(order_.front());
            if (it != map_.end()) { bytes_ -= it->second->size() * sizeof(float); map_.erase(it); }
            order_.erase(order_.begin());
        }
    }

    Renderer::Renderer(std::map<std::string, Voicebank> banks, std::shared_ptr<PhraseCache> cache, std::shared_ptr<const PronunciationEngine> pron)
        : banks_(std::move(banks)), cache_(std::move(cache)), pron_(std::move(pron)) {}

    double Renderer::project_end_sec(const Project& p, double tail) { return p.tempo_map.tick_to_sec(p.end_tick()) + tail; }

    Result<std::vector<float>> Renderer::render_track(const Project& p, const Track& t, const RenderOptions& o, const ProgressFn& progress,
                                                      const CancelToken* cancel, std::vector<Diagnostic>* diags) const {
        using R = Result<std::vector<float>>;
        const double t1 = o.t1 >= 0 ? o.t1 : project_end_sec(p, o.tail_sec);
        if (t1 < o.t0) return R::fail("render region ends before it starts");
        const size_t frames = static_cast<size_t>(std::llround((t1 - o.t0) * o.sample_rate));
        std::vector<float> out(frames, 0.f);
        auto bit = banks_.find(t.voicebank_id);
        if (bit == banks_.end() || !bit->second.usable()) return R::fail("voicebank '" + t.voicebank_id + "' is missing or unusable");
        const Voicebank& vb = bit->second;

        // Select the phrases that touch the region (a phrase's audio extends 0.6 s past its last note).
        std::vector<BuiltPlan> plans;
        for (const PhraseSpan& ph : split_phrases(t, p.tempo_map)) {
            if (ph.end_sec + 0.6 < o.t0 || ph.start_sec - kPhraseLeadSec > t1) continue;
            BuiltPlan b = build_phrase_plan(p, t, vb, *pron_, ph);
            if (diags) for (auto& d : b.diagnostics) diags->push_back(d);
            if (!b.plan.notes.empty()) plans.push_back(std::move(b));
        }
        if (plans.empty()) { if (progress) progress(1.0, t.name); return R::success(std::move(out)); }

        // Render phrases in parallel (each is independent and deterministic).
        std::vector<std::shared_ptr<const std::vector<float>>> audio(plans.size());
        std::vector<std::string> errors(plans.size());
        std::atomic<size_t> next{ 0 }, done{ 0 };
        int nthreads = o.threads > 0 ? o.threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        nthreads = std::min<int>(nthreads, static_cast<int>(plans.size()));
        auto work = [&]() {
            for (;;) {
                if (cancel && cancel->cancelled()) return;
                const size_t i = next.fetch_add(1);
                if (i >= plans.size()) return;
                const uint64_t key = fnv1a64(&o.sample_rate, sizeof o.sample_rate, plans[i].hash);
                auto cached = cache_->get(key);
                if (!cached) {
                    auto r = render_plan(plans[i].plan, o.sample_rate);
                    if (!r) errors[i] = r.error;
                    else { cached = std::make_shared<const std::vector<float>>(std::move(*r)); cache_->put(key, cached); }
                }
                audio[i] = cached;
                const size_t d = done.fetch_add(1) + 1;
                if (progress) progress(static_cast<double>(d) / static_cast<double>(plans.size()), t.name);
            }
        };
        if (nthreads <= 1) work();
        else { std::vector<std::thread> th; for (int i = 0; i < nthreads; ++i) th.emplace_back(work); for (auto& x : th) x.join(); }
        if (cancel && cancel->cancelled()) return R::fail("cancelled");

        for (size_t i = 0; i < plans.size(); ++i) {
            if (!errors[i].empty()) return R::fail(errors[i]);
            if (!audio[i]) continue;
            const long off = std::lround((plans[i].origin_sec - o.t0) * o.sample_rate);
            const auto& a = *audio[i];
            for (size_t k = 0; k < a.size(); ++k) {
                const long idx = off + static_cast<long>(k);
                if (idx < 0) continue;
                if (static_cast<size_t>(idx) >= out.size()) break;
                out[static_cast<size_t>(idx)] += a[k];
            }
        }
        return R::success(std::move(out));
    }

    Result<StereoAudio> Renderer::render_mix(const Project& p, const RenderOptions& o, const ProgressFn& progress, const CancelToken* cancel,
                                             std::vector<Diagnostic>* diags) const {
        using R = Result<StereoAudio>;
        const double t1 = o.t1 >= 0 ? o.t1 : project_end_sec(p, o.tail_sec);
        if (t1 < o.t0) return R::fail("render region ends before it starts");
        StereoAudio mix; mix.sample_rate = o.sample_rate; mix.start_sec = o.t0;
        const size_t frames = static_cast<size_t>(std::llround((t1 - o.t0) * o.sample_rate));
        mix.left.assign(frames, 0.f); mix.right.assign(frames, 0.f);
        bool any_solo = false; for (auto& t : p.tracks) any_solo = any_solo || t.solo;
        std::vector<const Track*> audible;
        for (auto& t : p.tracks) {
            if (o.apply_mute_solo) { if (t.mute) continue; if (any_solo && !t.solo) continue; }
            audible.push_back(&t);
        }
        RenderOptions to = o; to.t1 = t1;
        for (size_t ti = 0; ti < audible.size(); ++ti) {
            const Track& t = *audible[ti];
            ProgressFn sub = progress ? ProgressFn([&, ti](double f, const std::string& m) { progress((static_cast<double>(ti) + f) / static_cast<double>(audible.size()), m); }) : ProgressFn();
            auto r = render_track(p, t, to, sub, cancel, diags);
            if (!r) return R::fail(r.error);
            const float g = std::pow(10.f, t.gain_db / 20.f);
            const float gl = g * (t.pan <= 0.f ? 1.f : 1.f - t.pan), gr = g * (t.pan >= 0.f ? 1.f : 1.f + t.pan);
            for (size_t i = 0; i < frames && i < r->size(); ++i) { mix.left[i] += (*r)[i] * gl; mix.right[i] += (*r)[i] * gr; }
        }
        if (progress) progress(1.0, "done");
        return R::success(std::move(mix));
    }
}
