#include "test_util.hpp"
using namespace melovio; using namespace tu;

static std::shared_ptr<MixBuffer> ramp_mix(double start, double seconds, int sr = 1000) {
    auto m = std::make_shared<MixBuffer>(); m->sample_rate = sr; m->start_sec = start;
    const size_t n = static_cast<size_t>(seconds * sr); m->interleaved.resize(n * 2);
    for (size_t i = 0; i < n; ++i) { m->interleaved[2 * i] = m->interleaved[2 * i + 1] = static_cast<float>(i + 1); }   // value = frame index + 1
    return m;
}

TEST_CASE("transport: play, pause, stop, seek") {
    Transport t; float buf[2 * 100];
    CHECK(t.state() == TransportState::Stopped);
    t.play(); CHECK(t.state() == TransportState::Stopped);                                  // nothing to play yet
    t.set_mix(ramp_mix(0, 1.0));
    t.fill(buf, 100); CHECK(buf[0] == 0.f);                                                  // stopped -> silence
    t.play(); CHECK(t.state() == TransportState::Playing);
    t.fill(buf, 100); CHECK(buf[0] == 1.f); CHECK(buf[2 * 99] == 100.f);
    CHECK(t.position_sec() == doctest::Approx(0.1));
    t.pause(); CHECK(t.state() == TransportState::Paused);
    t.fill(buf, 100); CHECK(buf[0] == 0.f); CHECK(t.position_sec() == doctest::Approx(0.1));  // paused: position holds
    t.play(); t.fill(buf, 10); CHECK(buf[0] == 101.f);                                       // resumes where it left off
    t.seek(0.5); t.fill(buf, 1); CHECK(buf[0] == 501.f);
    t.set_return_position(0.2); t.stop();
    CHECK(t.state() == TransportState::Stopped); CHECK(t.position_sec() == doctest::Approx(0.2));
}
TEST_CASE("transport: reaching the end stops; play restarts") {
    Transport t; float buf[2 * 100]; t.set_mix(ramp_mix(0, 0.15)); t.play();
    t.fill(buf, 100); CHECK(t.state() == TransportState::Playing);
    t.fill(buf, 100);                                                                       // 50 real frames then silence
    CHECK(buf[2 * 49] == 150.f); CHECK(buf[2 * 50] == 0.f);
    CHECK(t.state() == TransportState::Stopped); CHECK(t.finished());
    t.play(); CHECK(t.state() == TransportState::Playing); CHECK(t.position_sec() == doctest::Approx(0.0)); CHECK_FALSE(t.finished());
}
TEST_CASE("transport: loop region wraps seamlessly") {
    Transport t; float buf[2 * 50]; t.set_mix(ramp_mix(0, 1.0));
    t.set_loop(true, 0.1, 0.2);                                                              // frames 100..199
    t.seek(0.18); t.play();
    t.fill(buf, 50);                                                                         // 20 frames to the end of the loop, then wrap
    CHECK(buf[0] == 181.f); CHECK(buf[2 * 19] == 200.f); CHECK(buf[2 * 20] == 101.f);       // frame 199 is followed by frame 100
    CHECK(t.state() == TransportState::Playing);
    for (int i = 0; i < 20; ++i) t.fill(buf, 50);                                            // plays far longer than the 1 s buffer: never ends
    CHECK(t.state() == TransportState::Playing);
    CHECK(t.position_sec() >= 0.1); CHECK(t.position_sec() < 0.2 + 1e-9);
    t.set_loop(false, 0, 0); CHECK(t.state() == TransportState::Playing);
}
TEST_CASE("transport: mix start offset and swapping audio while playing") {
    Transport t; float buf[2 * 10]; t.set_mix(ramp_mix(2.0, 1.0)); t.seek(2.0); t.play();   // buffer begins at project time 2 s
    t.fill(buf, 10); CHECK(buf[0] == 1.f); CHECK(t.position_sec() == doctest::Approx(2.01));
    t.set_mix(ramp_mix(2.0, 2.0));                                                           // new render replaces it; position is kept
    t.fill(buf, 10); CHECK(buf[0] == 11.f); CHECK(t.state() == TransportState::Playing);
    t.seek(1.0); t.fill(buf, 10); CHECK(buf[0] == 0.f); CHECK(buf[2 * 9] == 0.f);            // before the buffer: silence, position still advances
    CHECK(t.position_sec() == doctest::Approx(1.01));
}
TEST_CASE("player: asynchronous render, play, stop, preview, loop (real synthesis, manual audio device)") {
    ManualAudioOutput dev; Player pl(dev, banks());
    std::string err; REQUIRE(pl.open_device(44100, &err));
    Project p = small_project();
    CHECK(pl.status() == PlayerStatus::Idle); CHECK(pl.dirty());
    pl.render_async(p, true, 0.0);
    pl.wait_idle();
    CHECK(pl.status() == PlayerStatus::Ready); CHECK_FALSE(pl.dirty());
    CHECK(pl.transport().state() == TransportState::Playing);
    std::vector<float> out(2 * 4410);
    dev.pump(out.data(), 4410);
    double e = 0; for (float x : out) e += static_cast<double>(x) * x; CHECK(e > 0.1);                                  // sound comes out
    CHECK(pl.transport().position_sec() == doctest::Approx(0.1).epsilon(0.02));
    pl.pause(); CHECK(pl.transport().state() == TransportState::Paused); CHECK_FALSE(dev.running());
    pl.resume_or_play(); CHECK(pl.transport().state() == TransportState::Playing); CHECK(dev.running());
    pl.stop(); CHECK(pl.transport().position_sec() == doctest::Approx(0.0)); CHECK_FALSE(dev.running());
    // playback from the cursor
    pl.transport().seek(1.0); pl.resume_or_play(); dev.pump(out.data(), 4410); CHECK(pl.transport().position_sec() == doctest::Approx(1.1).epsilon(0.02));
    // preview of a region: plays that region only
    pl.stop(); pl.preview_async(p, 0.5, 1.0); pl.wait_idle();
    CHECK(pl.transport().state() == TransportState::Playing); CHECK(pl.dirty());                                          // a preview invalidates the full mix
    CHECK(pl.transport().position_sec() == doctest::Approx(0.5));
    for (int i = 0; i < 6; ++i) dev.pump(out.data(), 4410);
    CHECK(pl.transport().state() == TransportState::Stopped); CHECK(pl.transport().finished());
    // loop playback over a region of the full mix
    pl.render_async(p, false); pl.wait_idle(); pl.transport().set_loop(true, 0.5, 1.0); pl.transport().seek(0.9); pl.resume_or_play();
    for (int i = 0; i < 30; ++i) dev.pump(out.data(), 4410);
    CHECK(pl.transport().state() == TransportState::Playing); CHECK(pl.transport().position_sec() < 1.0 + 1e-9); CHECK(pl.transport().position_sec() >= 0.5);
}
TEST_CASE("player: render failures are reported, not hidden") {
    ManualAudioOutput dev; Player pl(dev, {});                                           // no voicebanks at all
    REQUIRE(pl.open_device(44100));
    pl.render_async(small_project(), true); pl.wait_idle();
    CHECK(pl.status() == PlayerStatus::Failed); CHECK(pl.last_error().find("yune") != std::string::npos);
    CHECK(pl.transport().state() == TransportState::Stopped);
    pl.update_voicebanks(banks()); pl.render_async(small_project(), false); pl.wait_idle(); CHECK(pl.status() == PlayerStatus::Ready);
}
TEST_CASE("player: a newer request supersedes an older one") {
    ManualAudioOutput dev; Player pl(dev, banks()); REQUIRE(pl.open_device(44100));
    Project big = small_project(); for (int i = 3; i < 60; ++i) { Note n; n.tick = i * 480 * 2; n.length = 400; n.lyric = "ら"; big.add_note(big.tracks[0], n); }
    pl.render_async(big, false); pl.render_async(small_project(), false); pl.wait_idle();
    CHECK(pl.status() == PlayerStatus::Ready);
    pl.cancel_render();                                                                  // cancelling when idle is harmless
}
