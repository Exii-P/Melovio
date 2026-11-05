// SDL2 implementation of IAudioOutput (callback-driven, float32 stereo).
#pragma once
#include "core/audio_output.hpp"

namespace melovio {
    class SdlAudioOutput : public IAudioOutput {
    public:
        SdlAudioOutput() = default;
        ~SdlAudioOutput() override { close(); }
        bool open(int sample_rate, Callback cb, std::string* error = nullptr) override;
        void close() override;
        void set_running(bool running) override;
        int sample_rate() const override { return rate_; }
        std::string backend_name() const override;
    private:
        unsigned device_ = 0;
        int rate_ = 0;
        Callback cb_;
        static void trampoline(void* user, unsigned char* stream, int len);
    };
}