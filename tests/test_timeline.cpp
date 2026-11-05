#include "test_util.hpp"
using namespace melovio;

TEST_CASE("tempo map: constant tempo") {
    TempoMap tm; tm.set_tempi({ { 0, 120.0 } });
    CHECK(tm.tick_to_sec(480) == doctest::Approx(0.5));
    CHECK(tm.sec_to_tick(1.0) == 960);
    CHECK(tm.bpm_at(1000) == doctest::Approx(120.0));
}
TEST_CASE("tempo map: tempo change") {
    TempoMap tm; tm.set_tempi({ { 0, 120.0 }, { 960, 60.0 } });      // 2 beats at 120 (1 s), then 60 bpm (1 s per beat)
    CHECK(tm.tick_to_sec(960) == doctest::Approx(1.0));
    CHECK(tm.tick_to_sec(1440) == doctest::Approx(1.5 + 0.5));        // half a beat... 480 ticks at 60bpm = 1 s => 960+480 -> 2.0
    CHECK(tm.sec_to_tick(2.0) == 1440);
    CHECK(tm.bpm_at(959) == doctest::Approx(120.0));
    CHECK(tm.bpm_at(960) == doctest::Approx(60.0));
}
TEST_CASE("tempo map: round trip over many ticks") {
    TempoMap tm; tm.set_tempi({ { 0, 93.0 }, { 1920, 140.0 }, { 5000, 70.5 } });
    for (Tick t = 0; t < 20000; t += 137) CHECK(tm.sec_to_tick(tm.tick_to_sec(t)) == t);
}
TEST_CASE("tempo map: invalid input is repaired") {
    TempoMap tm; tm.set_tempi({ { 500, 5000.0 } });
    REQUIRE(tm.tempi().size() == 2);
    CHECK(tm.tempi()[0].tick == 0);
    CHECK(tm.tempi()[1].bpm <= 400.0);
    TempoMap sig; sig.set_signatures({ { 0, 4, 3 } });                // denominator 3 is not valid
    CHECK(sig.signatures()[0].denominator == 4);
}
TEST_CASE("time signatures: bars and beats") {
    TempoMap tm; tm.set_signatures({ { 0, 4, 4 }, { 2, 3, 4 }, { 4, 6, 8 } });
    CHECK(tm.bar_start_tick(1) == 1920);
    CHECK(tm.bar_start_tick(2) == 3840);
    CHECK(tm.ticks_in_bar(2) == 1440);                                // 3/4
    CHECK(tm.bar_start_tick(4) == 3840 + 2 * 1440);
    CHECK(tm.ticks_in_bar(4) == 1440);                                // 6/8 = 6 eighths = 1440
    CHECK(tm.bar_at_tick(3839) == 1);
    CHECK(tm.bar_at_tick(3840) == 2);
    auto bb = tm.bar_beat(3840 + 1440 + 480 + 10);
    CHECK(bb.bar == 3); CHECK(bb.beat == 1); CHECK(bb.tick_in_beat == 10);
}
TEST_CASE("grid: durations and triplets") {
    CHECK(grid_ticks({ 4, false }, 480) == 480);
    CHECK(grid_ticks({ 8, false }, 480) == 240);
    CHECK(grid_ticks({ 8, true }, 480) == 160);                       // eighth-note triplet
    CHECK(grid_ticks({ 16, true }, 480) == 80);
    CHECK(snap_to_grid(250, 240) == 240);
    CHECK(snap_to_grid(370, 240) == 480);
    CHECK(snap_down(479, 240) == 240);
    CHECK(snap_down(-1, 240) == -240);
}
