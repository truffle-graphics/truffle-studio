#include "model/document.hpp"
#include "test.hpp"

using studio::model::Document;
using studio::model::DocumentState;
using studio::model::UndoHistory;

TEST(history_records_and_reverts_edits) {
    UndoHistory<int> history(1);
    history.begin("set 2") = 2;
    CHECK(history.commit());
    history.begin("set 3") = 3;
    CHECK(history.commit());
    CHECK(history.undo_label() == "set 3");
    CHECK(history.undo());
    CHECK(history.current() == 2);
    CHECK(history.undo());
    CHECK(history.current() == 1);
    CHECK(!history.undo());
    CHECK(history.redo());
    CHECK(history.current() == 2);
    CHECK(history.redo_label() == "set 3");
}

TEST(history_skips_unchanged_edits) {
    UndoHistory<int> history(5);
    history.begin("noop") = 5;
    CHECK(!history.commit());
    CHECK(!history.can_undo());
}

TEST(history_groups_nested_edits) {
    UndoHistory<int> history(0);
    history.begin("drag") = 1;
    history.begin("inner") = 2;
    CHECK(!history.commit());
    CHECK(history.editing());
    history.editable() = 3;
    CHECK(history.commit());
    CHECK(history.undo_label() == "drag");
    CHECK(history.undo());
    CHECK(history.current() == 0);
}

TEST(history_new_edit_clears_redo) {
    UndoHistory<int> history(0);
    history.begin("a") = 1;
    history.commit();
    history.undo();
    CHECK(history.can_redo());
    history.begin("b") = 2;
    history.commit();
    CHECK(!history.can_redo());
}

TEST(history_cancel_restores) {
    UndoHistory<int> history(4);
    history.begin("drag") = 9;
    history.cancel();
    CHECK(history.current() == 4);
    CHECK(!history.editing());
    CHECK(!history.can_undo());
}

TEST(history_blocks_undo_during_edit) {
    UndoHistory<int> history(0);
    history.begin("a") = 1;
    history.commit();
    history.begin("b") = 2;
    CHECK(!history.can_undo());
    CHECK(!history.undo());
    history.commit();
}

TEST(document_notifies_and_counts_revisions) {
    Document document;
    int heard = 0;
    document.subscribe([&] { ++heard; });
    CHECK(document.edit("rename", [](DocumentState& state) { state.name = "Bracket"; }));
    CHECK(heard == 1);
    CHECK(!document.edit("same", [](DocumentState& state) { state.name = "Bracket"; }));
    CHECK(heard == 1);
    CHECK(document.undo());
    CHECK(document.state().name == "Untitled");
    CHECK(heard == 2);
    CHECK(document.revision() == 2);
}

TEST(document_groups_updates) {
    Document document;
    int heard = 0;
    document.subscribe([&] { ++heard; });
    document.begin("drag");
    document.update([](DocumentState& state) { state.name = "a"; });
    document.update([](DocumentState& state) { state.name = "ab"; });
    CHECK(document.commit());
    CHECK(heard == 3);
    CHECK(document.undo());
    CHECK(document.state().name == "Untitled");
    CHECK(!document.can_undo());
}

TEST(document_allocates_unique_ids) {
    Document document;
    studio::model::ObjectId a = 0;
    studio::model::ObjectId b = 0;
    document.edit("ids", [&](DocumentState& state) {
        a = Document::allocate(state);
        b = Document::allocate(state);
    });
    CHECK(a != 0);
    CHECK(a != b);
    document.undo();
    studio::model::ObjectId c = 0;
    document.edit("again", [&](DocumentState& state) { c = Document::allocate(state); });
    CHECK(c == a);
}
