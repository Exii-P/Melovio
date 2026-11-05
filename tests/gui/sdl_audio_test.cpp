// Exercises the real SDL audio backend with SDL's "dummy" driver (no sound card needed): the device must pull the callback from its
// own thread while the transport plays a pre-rendered buffer.
#include "audio/sdl_audio_output.hpp"
#include "core/transport.hpp"
#include <chrono>
#include <cstdlib>
#include <doctest/doctest.h>
#include <thread>

TEST_CASE("sdl audio backend drives the transport from its own thread (dummy driver)") {
#ifdef _WIN32
    _putenv_s("SDL_AUDIODRIVER", "dummy");
#else
    setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
    melovio::SdlAudioOutput dev; melovio::Player pl(dev, {});
    std::string err; REQUIRE_MESSAGE(pl.open_device(44100, &err), err);
    CHECK(dev.backend_name().find("SDL") != std::string::npos);
    auto m = std::make_shared<melovio::MixBuffer>(); m->sample_rate = dev.sample_rate(); m->interleaved.assign(2 * static_cast<size_t>(dev.sample_rate()) * 2, 0.25f);
    pl.transport().set_mix(m); pl.resume_or_play();
    for (int i = 0; i < 100 && pl.transport().position_sec() < 0.2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(pl.transport().position_sec() >= 0.2);                                       // the audio thread advanced the transport
    pl.pause(); const double held = pl.transport().position_sec();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(pl.transport().position_sec() == doctest::Approx(held).epsilon(0.05));
    pl.stop();
}
