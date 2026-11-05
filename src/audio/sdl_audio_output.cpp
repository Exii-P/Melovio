#include "sdl_audio_output.hpp"
#include <SDL.h>
#include <cstring>

namespace melovio {
    void SdlAudioOutput::trampoline(void* user, unsigned char* stream, int len) {
        auto* self = static_cast<SdlAudioOutput*>(user);
        const int frames = len / static_cast<int>(sizeof(float) * 2);
        if (self->cb_) self->cb_(reinterpret_cast<float*>(stream), frames);
        else std::memset(stream, 0, static_cast<size_t>(len));
    }

    bool SdlAudioOutput::open(int sample_rate, Callback cb, std::string* error) {
        close();
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { if (error) *error = SDL_GetError(); return false; }
        cb_ = std::move(cb);
        SDL_AudioSpec want{}, have{};
        want.freq = sample_rate; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
        want.callback = &SdlAudioOutput::trampoline; want.userdata = this;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);     // no changes allowed: we need exactly float32 stereo at this rate
        if (device_ == 0) { if (error) *error = SDL_GetError(); cb_ = nullptr; return false; }
        rate_ = have.freq;
        return true;
    }

    void SdlAudioOutput::close() {
        if (device_) { SDL_CloseAudioDevice(device_); device_ = 0; }
        cb_ = nullptr;
    }

    void SdlAudioOutput::set_running(bool running) { if (device_) SDL_PauseAudioDevice(device_, running ? 0 : 1); }

    std::string SdlAudioOutput::backend_name() const {
        const char* d = SDL_GetCurrentAudioDriver();
        return std::string("SDL2 (") + (d ? d : "no driver") + ")";
    }
}
