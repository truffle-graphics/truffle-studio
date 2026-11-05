#pragma once

#include "model/feature.hpp"
#include "model/history.hpp"
#include "model/sketch.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace studio::model {

// Everything a document contains. Workspaces read and edit this state; it
// has no dependency on Truffle or the geometry kernel.
struct DocumentState {
    std::string name = "Untitled";
    ObjectId next_id = 1;
    std::vector<Sketch> sketches;
    std::vector<Extrude> extrudes; // in creation order

    bool operator==(const DocumentState&) const = default;

    const Extrude* extrude(ObjectId id) const noexcept {
        for (const auto& e : extrudes) {
            if (e.id == id) { return &e; }
        }
        return nullptr;
    }
    Extrude* extrude(ObjectId id) noexcept {
        return const_cast<Extrude*>(static_cast<const DocumentState&>(*this).extrude(id));
    }
    const Sketch* sketch(ObjectId id) const noexcept {
        for (const auto& s : sketches) {
            if (s.id == id) { return &s; }
        }
        return nullptr;
    }
    Sketch* sketch(ObjectId id) noexcept {
        return const_cast<Sketch*>(static_cast<const DocumentState&>(*this).sketch(id));
    }
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

    // Returns a token for unsubscribe().
    std::uint64_t subscribe(Listener listener) {
        listeners_.push_back({++last_token_, std::move(listener)});
        return last_token_;
    }
    void unsubscribe(std::uint64_t token) {
        std::erase_if(listeners_, [&](const auto& entry) { return entry.first == token; });
    }

private:
    bool finish(bool changed_state) {
        if (changed_state) { changed(); }
        return changed_state;
    }
    void changed() {
        ++revision_;
        // Listeners may unsubscribe while being notified.
        const auto listeners = listeners_;
        for (const auto& [token, listener] : listeners) { listener(); }
    }

    UndoHistory<DocumentState> history_;
    std::vector<std::pair<std::uint64_t, Listener>> listeners_;
    std::uint64_t last_token_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace studio::model
