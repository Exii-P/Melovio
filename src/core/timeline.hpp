// Musical time: ticks <-> seconds with tempo changes, bars/beats with time-signature changes.
#pragma once
#include <cstdint>
#include <vector>

namespace melovio {
    using Tick = int64_t;
    constexpr int kDefaultPPQ = 480;

    struct TempoChange { Tick tick = 0; double bpm = 120.0; };
    struct TimeSigChange { int bar = 0; int numerator = 4; int denominator = 4; };   // bar is 0-based, takes effect at that bar's start

    struct BarBeat { int bar = 0; int beat = 0; Tick tick_in_beat = 0; };            // all 0-based

    class TempoMap {
    public:
        explicit TempoMap(int ppq = kDefaultPPQ) : ppq_(ppq) { rebuild(); }
        TempoMap(int ppq, std::vector<TempoChange> tempi, std::vector<TimeSigChange> sigs);

        int ppq() const { return ppq_; }
        const std::vector<TempoChange>& tempi() const { return tempi_; }
        const std::vector<TimeSigChange>& signatures() const { return sigs_; }

        // Always leaves at least one tempo (at tick 0) and one signature (at bar 0). Invalid values are clamped.
        void set_tempi(std::vector<TempoChange> t);
        void set_signatures(std::vector<TimeSigChange> s);
        void set_ppq(int ppq);

        double tick_to_sec(Tick t) const;
        Tick   sec_to_tick(double s) const;           // rounds to nearest tick
        double bpm_at(Tick t) const;

        Tick ticks_per_beat(int denominator) const { return static_cast<Tick>(ppq_) * 4 / denominator; }
        Tick bar_start_tick(int bar) const;
        int  bar_at_tick(Tick t) const;
        TimeSigChange signature_at_bar(int bar) const;
        Tick ticks_in_bar(int bar) const;
        BarBeat bar_beat(Tick t) const;

    private:
        struct Seg { Tick tick; double sec; double bpm; };
        struct BarSeg { int bar; Tick tick; int num; int den; };
        void rebuild();
        int ppq_;
        std::vector<TempoChange> tempi_;
        std::vector<TimeSigChange> sigs_;
        std::vector<Seg> segs_;
        std::vector<BarSeg> bsegs_;
    };

    // Note-length grid used by snap/quantize. `triplet` divides the grid by 3 per 2 (e.g. 1/8T).
    struct GridDivision { int denominator = 16; bool triplet = false; };
    Tick grid_ticks(const GridDivision& g, int ppq);
    Tick snap_to_grid(Tick t, Tick grid);             // nearest multiple
    Tick snap_down(Tick t, Tick grid);
}
