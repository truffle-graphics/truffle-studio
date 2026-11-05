#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace studio::model {

// Snapshot-based undo history for a copyable state. Each edit records the
// state before it; an open edit groups any number of changes (for example a
// drag) into one undo step.
template <typename State> class UndoHistory {
public:
    explicit UndoHistory(State initial = {}) : current_(std::move(initial)) {}

    const State& current() const noexcept { return current_; }

    // Starts an edit. Nested calls join the outermost edit.
    State& begin(std::string label) {
        if (depth_++ == 0) { open_ = Entry{std::move(label), current_}; }
        return current_;
    }

    // Ends an edit. An edit that left the state unchanged records nothing.
    // Returns true when an undo step was recorded.
    bool commit() {
        if (depth_ == 0 || --depth_ > 0) { return false; }
        auto entry = std::move(*open_);
        open_.reset();
        if (entry.state == current_) { return false; }
        undo_.push_back(std::move(entry));
        redo_.clear();
        return true;
    }

    // Abandons an open edit and restores the state from before it.
    void cancel() {
        if (depth_ == 0) { return; }
        depth_ = 0;
        current_ = std::move(open_->state);
        open_.reset();
    }

    // The state being edited. Only valid while an edit is open.
    State& editable() {
        if (depth_ == 0) { throw std::logic_error("UndoHistory: no edit is open"); }
        return current_;
    }

    bool editing() const noexcept { return depth_ > 0; }
    bool can_undo() const noexcept { return depth_ == 0 && !undo_.empty(); }
    bool can_redo() const noexcept { return depth_ == 0 && !redo_.empty(); }
    std::string undo_label() const { return undo_.empty() ? std::string() : undo_.back().label; }
    std::string redo_label() const { return redo_.empty() ? std::string() : redo_.back().label; }

    bool undo() { return step(undo_, redo_); }
    bool redo() { return step(redo_, undo_); }

private:
    struct Entry {
        std::string label;
        State state;
    };

    bool step(std::vector<Entry>& from, std::vector<Entry>& to) {
        if (depth_ > 0 || from.empty()) { return false; }
        auto entry = std::move(from.back());
        from.pop_back();
        std::swap(entry.state, current_);
        to.push_back(std::move(entry));
        return true;
    }

    State current_;
    std::optional<Entry> open_;
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    std::size_t depth_ = 0;
};

} // namespace studio::model
