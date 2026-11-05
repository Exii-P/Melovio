// Undo/redo by whole-project snapshots. Project data is small (notes are plain structs), so snapshots are simpler and safer
// than per-command inverse operations: every edit is atomic and undo can never drift out of sync.
#pragma once
#include "project.hpp"
#include <deque>

namespace melovio {
    class History {
    public:
        explicit History(size_t limit = 200) : limit_(limit) {}
        // Call BEFORE mutating `current`. Consecutive pushes with the same non-empty coalesce key (e.g. dragging a slider) are merged.
        void record(const Project& current, const std::string& label, const std::string& coalesce_key = "");
        bool can_undo() const { return !undo_.empty(); }
        bool can_redo() const { return !redo_.empty(); }
        bool undo(Project& current);
        bool redo(Project& current);
        const std::string& undo_label() const;
        const std::string& redo_label() const;
        void clear() { undo_.clear(); redo_.clear(); last_key_.clear(); }
        // Ends the current coalescing run (call when a drag finishes).
        void break_coalescing() { last_key_.clear(); }
    private:
        struct Entry { Project snapshot; std::string label; };
        std::deque<Entry> undo_, redo_;
        size_t limit_;
        std::string last_key_;
    };
}
