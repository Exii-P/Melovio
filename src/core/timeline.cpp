#include "timeline.hpp"
#include <algorithm>
#include <cmath>

namespace melovio {
    TempoMap::TempoMap(int ppq, std::vector<TempoChange> tempi, std::vector<TimeSigChange> sigs) : ppq_(ppq > 0 ? ppq : kDefaultPPQ) {
        set_tempi(std::move(tempi)); set_signatures(std::move(sigs));
    }

    void TempoMap::set_ppq(int ppq) { ppq_ = ppq > 0 ? ppq : kDefaultPPQ; rebuild(); }

    void TempoMap::set_tempi(std::vector<TempoChange> t) { tempi_ = std::move(t); rebuild(); }
    void TempoMap::set_signatures(std::vector<TimeSigChange> s) { sigs_ = std::move(s); rebuild(); }

    void TempoMap::rebuild() {
        for (auto& t : tempi_) { t.tick = std::max<Tick>(0, t.tick); t.bpm = std::clamp(t.bpm, 10.0, 400.0); }
        std::stable_sort(tempi_.begin(), tempi_.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
        // later entry at the same tick wins
        std::vector<TempoChange> u;
        for (auto& t : tempi_) { if (!u.empty() && u.back().tick == t.tick) u.back() = t; else u.push_back(t); }
        tempi_ = u;
        if (tempi_.empty() || tempi_.front().tick != 0) tempi_.insert(tempi_.begin(), TempoChange{ 0, tempi_.empty() ? 120.0 : tempi_.front().bpm });

        static const int ok_den[] = { 1, 2, 4, 8, 16, 32 };
        for (auto& s : sigs_) {
            s.bar = std::max(0, s.bar); s.numerator = std::clamp(s.numerator, 1, 32);
            if (std::find(std::begin(ok_den), std::end(ok_den), s.denominator) == std::end(ok_den)) s.denominator = 4;
        }
        std::stable_sort(sigs_.begin(), sigs_.end(), [](auto& a, auto& b) { return a.bar < b.bar; });
        std::vector<TimeSigChange> us;
        for (auto& s : sigs_) { if (!us.empty() && us.back().bar == s.bar) us.back() = s; else us.push_back(s); }
        sigs_ = us;
        if (sigs_.empty() || sigs_.front().bar != 0) sigs_.insert(sigs_.begin(), TimeSigChange{ 0, 4, 4 });

        segs_.clear();
        double sec = 0;
        for (size_t i = 0; i < tempi_.size(); ++i) {
            if (i > 0) sec += static_cast<double>(tempi_[i].tick - tempi_[i - 1].tick) / ppq_ * 60.0 / tempi_[i - 1].bpm;
            segs_.push_back({ tempi_[i].tick, sec, tempi_[i].bpm });
        }
        bsegs_.clear();
        Tick tick = 0;
        for (size_t i = 0; i < sigs_.size(); ++i) {
            if (i > 0) {
                const Tick per_bar = static_cast<Tick>(ppq_) * 4 * bsegs_.back().num / bsegs_.back().den;
                tick += per_bar * (sigs_[i].bar - sigs_[i - 1].bar);
            }
            bsegs_.push_back({ sigs_[i].bar, tick, sigs_[i].numerator, sigs_[i].denominator });
        }
    }

    double TempoMap::tick_to_sec(Tick t) const {
        if (t < 0) return static_cast<double>(t) / ppq_ * 60.0 / segs_.front().bpm;
        auto it = std::upper_bound(segs_.begin(), segs_.end(), t, [](Tick v, const Seg& s) { return v < s.tick; });
        const Seg& s = *(it - 1);
        return s.sec + static_cast<double>(t - s.tick) / ppq_ * 60.0 / s.bpm;
    }

    Tick TempoMap::sec_to_tick(double sec) const {
        if (sec <= 0) return static_cast<Tick>(std::llround(sec * segs_.front().bpm / 60.0 * ppq_));
        auto it = std::upper_bound(segs_.begin(), segs_.end(), sec, [](double v, const Seg& s) { return v < s.sec; });
        const Seg& s = *(it - 1);
        return s.tick + static_cast<Tick>(std::llround((sec - s.sec) * s.bpm / 60.0 * ppq_));
    }

    double TempoMap::bpm_at(Tick t) const {
        auto it = std::upper_bound(segs_.begin(), segs_.end(), t, [](Tick v, const Seg& s) { return v < s.tick; });
        return it == segs_.begin() ? segs_.front().bpm : (it - 1)->bpm;
    }

    TimeSigChange TempoMap::signature_at_bar(int bar) const {
        auto it = std::upper_bound(bsegs_.begin(), bsegs_.end(), bar, [](int v, const BarSeg& s) { return v < s.bar; });
        const BarSeg& s = *(it == bsegs_.begin() ? it : it - 1);
        return { s.bar, s.num, s.den };
    }

    Tick TempoMap::ticks_in_bar(int bar) const {
        auto s = signature_at_bar(bar);
        return static_cast<Tick>(ppq_) * 4 * s.numerator / s.denominator;
    }

    Tick TempoMap::bar_start_tick(int bar) const {
        bar = std::max(0, bar);
        auto it = std::upper_bound(bsegs_.begin(), bsegs_.end(), bar, [](int v, const BarSeg& s) { return v < s.bar; });
        const BarSeg& s = *(it - 1);
        return s.tick + static_cast<Tick>(bar - s.bar) * (static_cast<Tick>(ppq_) * 4 * s.num / s.den);
    }

    int TempoMap::bar_at_tick(Tick t) const {
        if (t <= 0) return 0;
        auto it = std::upper_bound(bsegs_.begin(), bsegs_.end(), t, [](Tick v, const BarSeg& s) { return v < s.tick; });
        const BarSeg& s = *(it - 1);
        const Tick per = static_cast<Tick>(ppq_) * 4 * s.num / s.den;
        return s.bar + static_cast<int>((t - s.tick) / std::max<Tick>(1, per));
    }

    BarBeat TempoMap::bar_beat(Tick t) const {
        BarBeat b; b.bar = bar_at_tick(t);
        const Tick in_bar = std::max<Tick>(0, t - bar_start_tick(b.bar));
        const Tick pb = ticks_per_beat(signature_at_bar(b.bar).denominator);
        b.beat = static_cast<int>(in_bar / pb); b.tick_in_beat = in_bar % pb;
        return b;
    }

    Tick grid_ticks(const GridDivision& g, int ppq) {
        Tick base = static_cast<Tick>(ppq) * 4 / std::max(1, g.denominator);
        return g.triplet ? base * 2 / 3 : base;
    }
    Tick snap_to_grid(Tick t, Tick grid) {
        if (grid <= 0) return t;
        return static_cast<Tick>(std::llround(static_cast<double>(t) / static_cast<double>(grid))) * grid;
    }
    Tick snap_down(Tick t, Tick grid) {
        if (grid <= 0) return t;
        Tick q = t / grid; if (t < 0 && t % grid) --q; return q * grid;
    }
}
