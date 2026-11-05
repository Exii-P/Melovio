// Transport (play/pause/stop/seek/loop) and the Player that renders asynchronously and feeds the audio device.
//
// Real-time rules: the audio callback only reads an immutable pre-rendered buffer and a few atomics. All synthesis happens on a
// worker thread; the GUI thread never blocks on it.
#pragma once
#include "audio_output.hpp"
#include "renderer.hpp"
#include <condition_variable>
#include <thread>

namespace melovio {
    enum class TransportState { Stopped, Playing, Paused };

    // Immutable rendered audio that the transport plays.
    struct MixBuffer {
        int sample_rate = 44100;
        double start_sec = 0;                 // project time of frame 0
        std::vector<float> interleaved;       // stereo
        size_t frames() const { return interleaved.size() / 2; }
    };

    class Transport {
    public:
        Transport() = default;
        // Swaps in new audio. Safe to call while playing; position (project seconds) is preserved. Not real-time safe (call from non-audio thread).
        void set_mix(std::shared_ptr<const MixBuffer> mix);
        // Audio-thread entry: writes `frames` stereo frames (silence when not playing).
        void fill(float* out, int frames);

        void play();                          // from current position (rewinds to loop start / 0 if at the end)
        void pause();
        void stop();                          // stops and returns to `stop_position()`
        void seek(double project_sec);
        void set_loop(bool enabled, double start_sec, double end_sec);
        void set_return_position(double sec) { return_pos_.store(sec); }     // where Stop returns to (play-start marker)
        TransportState state() const { return static_cast<TransportState>(state_.load()); }
        double position_sec() const;
        bool has_audio() const { return mix_ptr_.load() != nullptr; }
        bool finished() const { return finished_.load(); }   // set when playback ran off the end (cleared by play/seek)

    private:
        std::shared_ptr<const MixBuffer> mix_owner_;           // keeps the active buffer alive (non-RT side)
        std::shared_ptr<const MixBuffer> retired_[3];
        std::atomic<const MixBuffer*> mix_ptr_{ nullptr };
        std::atomic<const MixBuffer*> in_use_{ nullptr };
        std::atomic<int> state_{ 0 };
        std::atomic<double> pos_sec_{ 0.0 };                   // project time of the next frame to play
        std::atomic<double> return_pos_{ 0.0 };
        std::atomic<bool> loop_on_{ false }, finished_{ false };
        std::atomic<double> loop_a_{ 0.0 }, loop_b_{ 0.0 };
    };

    enum class PlayerStatus { Idle, Rendering, Ready, Failed };

    class Player {
    public:
        Player(IAudioOutput& out, std::map<std::string, Voicebank> banks, std::shared_ptr<PhraseCache> cache = std::make_shared<PhraseCache>());
        ~Player();
        Player(const Player&) = delete; Player& operator=(const Player&) = delete;

        bool open_device(int sample_rate, std::string* error = nullptr);
        void update_voicebanks(std::map<std::string, Voicebank> banks);

        // Starts an asynchronous render of the whole project (reuses cached phrases). `then_play`: begin playback at `from_sec` when done.
        void render_async(const Project& snapshot, bool then_play, double from_sec = 0.0);
        // Renders only [t0,t1] (note/phrase/selection preview) and plays it from its start.
        void preview_async(const Project& snapshot, double t0, double t1);
        void cancel_render();
        PlayerStatus status() const { return status_.load(); }
        double render_progress() const { return progress_.load(); }
        std::string last_error() const;
        std::vector<Diagnostic> last_diagnostics() const;
        // The GUI sets this whenever the project changes; the next play() re-renders first.
        void mark_dirty() { dirty_.store(true); }
        bool dirty() const { return dirty_.load(); }

        Transport& transport() { return transport_; }
        const Transport& transport() const { return transport_; }
        // Convenience wrappers
        void pause() { transport_.pause(); out_.set_running(transport_.state() == TransportState::Playing); }
        void stop() { transport_.stop(); out_.set_running(false); }
        void resume_or_play() { transport_.play(); out_.set_running(true); }
        void wait_idle();                      // blocks until a pending render finished (tests)

    private:
        void worker_main();
        struct Job { Project project; bool play = false; double from = 0; bool preview = false; double t0 = 0, t1 = 0; };
        IAudioOutput& out_;
        std::map<std::string, Voicebank> banks_;
        std::shared_ptr<PhraseCache> cache_;
        Transport transport_;
        std::thread worker_;
        mutable std::mutex m_;
        std::condition_variable cv_;
        bool quit_ = false, has_job_ = false, busy_ = false;
        Job job_;
        CancelToken cancel_;
        std::atomic<PlayerStatus> status_{ PlayerStatus::Idle };
        std::atomic<double> progress_{ 0.0 };
        std::atomic<bool> dirty_{ true };
        std::string error_;
        std::vector<Diagnostic> diags_;
        int sample_rate_ = 44100;
    };
}
