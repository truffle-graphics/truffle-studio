#pragma once

#include "model/history.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace studio::model {

// Stable identity for document objects; 0 is never assigned.
using ObjectId = std::uint64_t;

// Everything a document contains. Workspaces read and edit this state; it
// has no dependency on Truffle or the geometry kernel.
struct DocumentState {
    std::string name = "Untitled";
    ObjectId next_id = 1;

    bool operator==(const DocumentState&) const = default;
};

// The shared document: state, undo history and change notification.
class Document {
public:
    using Listener = std::function<void()>;

    const DocumentState& state() const noexcept { return history_.current(); }
    std::uint64_t revision() const noexcept { return revision_; }

    // Applies `change` as one undo step. Returns false if nothing changed.
    template <typename Change> bool edit(std::string label, Change&& change) {
        auto& state = history_.begin(std::move(label));
        std::forward<Change>(change)(state);
        return finish(history_.commit());
    }

    // Grouped edits (for example a drag): every `update` between begin and
    // commit becomes one undo step. Listeners hear each update.
    void begin(std::string label) { static_cast<void>(history_.begin(std::move(label))); }
    template <typename Change> void update(Change&& change) {
        std::forward<Change>(change)(history_.editable());
        changed();
    }
    bool commit() { return finish(history_.commit()); }
    void cancel() {
        history_.cancel();
        changed();
    }

    bool can_undo() const noexcept { return history_.can_undo(); }
    bool can_redo() const noexcept { return history_.can_redo(); }
    std::string undo_label() const { return history_.undo_label(); }
    std::string redo_label() const { return history_.redo_label(); }
    bool undo() { return finish(history_.undo()); }
    bool redo() { return finish(history_.redo()); }

    // Allocates an object id inside an edit.
    static ObjectId allocate(DocumentState& state) { return state.next_id++; }

    void subscribe(Listener listener) { listeners_.push_back(std::move(listener)); }

private:
    bool finish(bool changed_state) {
        if (changed_state) { changed(); }
        return changed_state;
    }
    void changed() {
        ++revision_;
        for (const auto& listener : listeners_) { listener(); }
    }

    UndoHistory<DocumentState> history_;
    std::vector<Listener> listeners_;
    std::uint64_t revision_ = 0;
};

} // namespace studio::model
