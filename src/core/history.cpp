#include "history.hpp"

namespace melovio {
    void History::record(const Project& current, const std::string& label, const std::string& key) {
        redo_.clear();
        if (!key.empty() && key == last_key_ && !undo_.empty()) return;   // same drag/slider run: keep the first snapshot
        undo_.push_back({ current, label });
        while (undo_.size() > limit_) undo_.pop_front();
        last_key_ = key;
    }
    bool History::undo(Project& cur) {
        if (undo_.empty()) return false;
        redo_.push_back({ cur, undo_.back().label });
        cur = std::move(undo_.back().snapshot); undo_.pop_back(); last_key_.clear(); return true;
    }
    bool History::redo(Project& cur) {
        if (redo_.empty()) return false;
        undo_.push_back({ cur, redo_.back().label });
        cur = std::move(redo_.back().snapshot); redo_.pop_back(); last_key_.clear(); return true;
    }
    const std::string& History::undo_label() const { static const std::string e; return undo_.empty() ? e : undo_.back().label; }
    const std::string& History::redo_label() const { static const std::string e; return redo_.empty() ? e : redo_.back().label; }
}