#include "transport.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace melovio {
    void Transport::set_mix(std::shared_ptr<const MixBuffer> mix) {
        std::shared_ptr<const MixBuffer> old = mix_owner_;
        mix_owner_ = std::move(mix);
        mix_ptr_.store(mix_owner_.get(), std::memory_order_release);
        // Wait (bounded) until the audio thread is no longer reading the old buffer, then park it so a late reader can't see a dangling pointer.
        if (old) {
            for (int i = 0; i < 200 && in_use_.load(std::memory_order_acquire) == old.get(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            static std::atomic<unsigned> slot{ 0 };
            retired_[slot.fetch_add(1) % 3] = std::move(old);
        }
    }

    void Transport::fill(float* out, int frames) {
        const MixBuffer* m = mix_ptr_.load(std::memory_order_acquire);
        in_use_.store(m, std::memory_order_release);
        if (m != mix_ptr_.load(std::memory_order_acquire)) { m = mix_ptr_.load(std::memory_order_acquire); in_use_.store(m, std::memory_order_release); }
        if (!m || state_.load(std::memory_order_acquire) != static_cast<int>(TransportState::Playing)) {
            std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
            in_use_.store(nullptr, std::memory_order_release);
            return;
        }
        const double sr = m->sample_rate;
        const long total = static_cast<long>(m->frames());
        long f = std::lround((pos_sec_.load(std::memory_order_relaxed) - m->start_sec) * sr);
        const bool loop = loop_on_.load(std::memory_order_relaxed);
        const long la = std::lround((loop_a_.load(std::memory_order_relaxed) - m->start_sec) * sr);
        const long lb = std::lround((loop_b_.load(std::memory_order_relaxed) - m->start_sec) * sr);
        const bool loop_ok = loop && lb > la + 16;
        bool ended = false;
        for (int i = 0; i < frames; ++i) {
            if (loop_ok && f >= lb) f = la;
            if (f >= total) { ended = true; std::memset(out + 2 * i, 0, sizeof(float) * 2 * static_cast<size_t>(frames - i)); break; }
            if (f < 0) { out[2 * i] = out[2 * i + 1] = 0.f; ++f; continue; }
            out[2 * i] = m->interleaved[static_cast<size_t>(f) * 2]; out[2 * i + 1] = m->interleaved[static_cast<size_t>(f) * 2 + 1];
            ++f;
        }
        pos_sec_.store(m->start_sec + static_cast<double>(std::min(f, total)) / sr, std::memory_order_relaxed);
        if (ended) { finished_.store(true); state_.store(static_cast<int>(TransportState::Stopped)); }
        in_use_.store(nullptr, std::memory_order_release);
    }

    void Transport::play() {
        if (!mix_ptr_.load()) return;
        const MixBuffer* m = mix_ptr_.load();
        const double end = m->start_sec + static_cast<double>(m->frames()) / m->sample_rate;
        double p = pos_sec_.load();
        if (finished_.load() || p >= end - 1e-6) p = loop_on_.load() ? loop_a_.load() : std::max(m->start_sec, return_pos_.load());
        pos_sec_.store(p);
        finished_.store(false);
        state_.store(static_cast<int>(TransportState::Playing));
    }
    void Transport::pause() { if (state() == TransportState::Playing) state_.store(static_cast<int>(TransportState::Paused)); }
    void Transport::stop() { state_.store(static_cast<int>(TransportState::Stopped)); pos_sec_.store(return_pos_.load()); finished_.store(false); }
    void Transport::seek(double s) { pos_sec_.store(std::max(0.0, s)); finished_.store(false); }
    void Transport::set_loop(bool on, double a, double b) { loop_a_.store(a); loop_b_.store(b); loop_on_.store(on && b > a); }
    double Transport::position_sec() const { return pos_sec_.load(); }

    Player::Player(IAudioOutput& out, std::map<std::string, Voicebank> banks, std::shared_ptr<PhraseCache> cache)
        : out_(out), banks_(std::move(banks)), cache_(std::move(cache)) {
        worker_ = std::thread([this] { worker_main(); });
    }

    Player::~Player() {
        { std::lock_guard<std::mutex> l(m_); quit_ = true; cancel_.cancel(); }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        out_.set_running(false);
        out_.close();
    }

    bool Player::open_device(int sr, std::string* err) {
        sample_rate_ = sr;
        return out_.open(sr, [this](float* o, int n) { transport_.fill(o, n); }, err);
    }

    void Player::update_voicebanks(std::map<std::string, Voicebank> banks) { std::lock_guard<std::mutex> l(m_); banks_ = std::move(banks); dirty_.store(true); }

    void Player::render_async(const Project& p, bool then_play, double from) {
        { std::lock_guard<std::mutex> l(m_); job_ = Job{ p, then_play, from, false, 0, 0 }; has_job_ = true; cancel_.cancel(); status_.store(PlayerStatus::Rendering); progress_.store(0.0); }
        cv_.notify_all();
    }
    void Player::preview_async(const Project& p, double t0, double t1) {
        { std::lock_guard<std::mutex> l(m_); job_ = Job{ p, true, t0, true, t0, t1 }; has_job_ = true; cancel_.cancel(); status_.store(PlayerStatus::Rendering); progress_.store(0.0); }
        cv_.notify_all();
    }
    void Player::cancel_render() { std::lock_guard<std::mutex> l(m_); cancel_.cancel(); has_job_ = false; }
    std::string Player::last_error() const { std::lock_guard<std::mutex> l(m_); return error_; }
    std::vector<Diagnostic> Player::last_diagnostics() const { std::lock_guard<std::mutex> l(m_); return diags_; }

    void Player::wait_idle() {
        std::unique_lock<std::mutex> l(m_);
        for (;;) {
            if (!has_job_ && !busy_) return;
            l.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); l.lock();
        }
    }

    void Player::worker_main() {
        for (;;) {
            Job job; std::map<std::string, Voicebank> banks;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return quit_ || has_job_; });
                if (quit_) return;
                job = std::move(job_); has_job_ = false; busy_ = true; banks = banks_;
                cancel_.reset();
            }
            Renderer r(std::move(banks), cache_);
            RenderOptions ro; ro.sample_rate = sample_rate_;
            if (job.preview) { ro.t0 = job.t0; ro.t1 = job.t1; }
            std::vector<Diagnostic> diags;
            auto res = r.render_mix(job.project, ro, [this](double f, const std::string&) { progress_.store(f); }, &cancel_, &diags);
            std::lock_guard<std::mutex> l(m_);
            busy_ = false;
            diags_ = diags;
            if (has_job_) continue;                      // superseded: skip publishing, the next job is already queued
            if (!res) {
                if (res.error == "cancelled") { status_.store(PlayerStatus::Idle); continue; }
                error_ = res.error; status_.store(PlayerStatus::Failed); continue;
            }
            auto mb = std::make_shared<MixBuffer>();
            mb->sample_rate = res->sample_rate; mb->start_sec = res->start_sec; mb->interleaved = res->interleaved(2);
            transport_.set_mix(mb);
            error_.clear();
            status_.store(PlayerStatus::Ready);
            dirty_.store(job.preview);                    // a region preview is not the full mix: next full play must re-render
            if (job.play) {
                if (job.preview) transport_.set_loop(false, 0, 0);        // previews never loop; full playback keeps the user's loop setting
                transport_.seek(job.from); transport_.play();
                out_.set_running(true);
            }
        }
    }
}
