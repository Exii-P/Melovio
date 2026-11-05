// Audio device abstraction. The core never talks to a sound card directly: playback pulls audio through this interface, so the GUI
// can use SDL, tests can use a manual/null device, and a future DAW integration can drive the transport from its own audio thread.
#pragma once
#include <functional>
#include <string>

namespace melovio {
    class IAudioOutput {
    public:
        virtual ~IAudioOutput() = default;
        // The callback runs on the device's real-time thread: it must not allocate, lock, or call into the synthesiser.
        // It must fill `frames` stereo frames of interleaved float samples.
        using Callback = std::function<void(float* interleaved_stereo, int frames)>;
        virtual bool open(int sample_rate, Callback cb, std::string* error = nullptr) = 0;
        virtual void close() = 0;
        virtual void set_running(bool running) = 0;      // start/stop pulling the callback
        virtual int sample_rate() const = 0;
        virtual std::string backend_name() const = 0;
    };

    // No device: the owner drives it with pump(). Used by tests and headless tools.
    class ManualAudioOutput : public IAudioOutput {
    public:
        bool open(int sr, Callback cb, std::string*) override { sr_ = sr; cb_ = std::move(cb); open_ = true; return true; }
        void close() override { open_ = false; running_ = false; cb_ = nullptr; }
        void set_running(bool r) override { running_ = r; }
        int sample_rate() const override { return sr_; }
        std::string backend_name() const override { return "manual"; }
        bool running() const { return running_; }
        void pump(float* out, int frames) { if (open_ && running_ && cb_) cb_(out, frames); }
    private:
        int sr_ = 0; Callback cb_; bool open_ = false, running_ = false;
    };
}
