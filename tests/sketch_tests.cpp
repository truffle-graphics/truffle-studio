#include "model/sketch_edit.hpp"
#include "test.hpp"

using namespace studio::model;

namespace {

Sketch& new_sketch(DocumentState& state) {
    return *state.sketch(add_sketch(state, SketchPlane::xy));
}

} // namespace

TEST(sketch_names_are_unique) {
    DocumentState state;
    const auto a = add_sketch(state, SketchPlane::xy);
    static_cast<void>(add_sketch(state, SketchPlane::xz));
    CHECK(state.sketch(a)->name == "Sketch 1");
    state.sketches.erase(state.sketches.begin());
    static_cast<void>(add_sketch(state, SketchPlane::yz));
    CHECK(state.sketches[0].name == "Sketch 2");
    CHECK(state.sketches[1].name == "Sketch 3");
    CHECK(state.sketches[1].plane == SketchPlane::yz);
}

TEST(sketch_lines_share_points) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto a = point_at(state, sketch, {0, 0}, 0.5);
    const auto b = point_at(state, sketch, {10, 0}, 0.5);
    CHECK(point_at(state, sketch, {10.2, 0.1}, 0.5) == b);
    CHECK(add_line(state, sketch, a, b) != 0);
    CHECK(add_line(state, sketch, b, a) == 0); // duplicate
    CHECK(add_line(state, sketch, a, a) == 0); // zero length
    CHECK(sketch.points.size() == 2);
    CHECK(sketch.lines.size() == 1);
}

TEST(sketch_rectangle_is_one_closed_loop) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    CHECK(!add_rectangle(state, sketch, {0, 0}, {0, 10}, 1e-6));
    CHECK(sketch.lines.size() == 4);
    CHECK(sketch.points.size() == 4);
    const auto loops = find_loops(sketch);
    CHECK(loops.closed.size() == 1);
    CHECK(loops.open == 0);
    const auto& edges = loops.closed[0].edges;
    CHECK(edges.size() == 4);
    // The walk is continuous: each edge starts where the previous one ended.
    const auto ends = [&](const LoopEdge& e) {
        for (const auto& l : sketch.lines) {
            if (l.id == e.curve) { return e.reversed ? std::pair{l.end, l.start} : std::pair{l.start, l.end}; }
        }
        return std::pair<ObjectId, ObjectId>{};
    };
    for (std::size_t i = 0; i < edges.size(); ++i) {
        CHECK(ends(edges[i]).second == ends(edges[(i + 1) % edges.size()]).first);
    }
}

TEST(sketch_loops_follow_reversed_edges_and_arcs) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto c = point_at(state, sketch, {0, 0}, 1e-6);
    const auto a = point_at(state, sketch, {5, 0}, 1e-6);
    const auto b = point_at(state, sketch, {-5, 0}, 1e-6);
    // A half disc: the arc runs a -> b, the line also runs a -> b.
    CHECK(add_arc(state, sketch, c, a, b) != 0);
    CHECK(add_line(state, sketch, a, b) != 0);
    const auto loops = find_loops(sketch);
    CHECK(loops.closed.size() == 1);
    CHECK(loops.closed[0].edges.size() == 2);
    CHECK(loops.closed[0].edges[0].reversed != loops.closed[0].edges[1].reversed);
    CHECK(add_arc(state, sketch, c, a, a) == 0);
}

TEST(sketch_open_and_branching_chains_are_not_profiles) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto p = [&](double x, double y) { return point_at(state, sketch, {x, y}, 1e-6); };
    static_cast<void>(add_line(state, sketch, p(0, 0), p(10, 0)));
    static_cast<void>(add_line(state, sketch, p(10, 0), p(10, 10)));
    CHECK(find_loops(sketch).closed.empty());
    CHECK(find_loops(sketch).open == 1);
    // Closing the triangle, then adding a branch, breaks it again.
    static_cast<void>(add_line(state, sketch, p(10, 10), p(0, 0)));
    CHECK(find_loops(sketch).closed.size() == 1);
    static_cast<void>(add_line(state, sketch, p(10, 10), p(20, 20)));
    CHECK(find_loops(sketch).closed.empty());
    CHECK(find_loops(sketch).open == 1);
}

TEST(sketch_circles_are_loops) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto c = point_at(state, sketch, {3, 4}, 1e-6);
    CHECK(add_circle(state, sketch, c, 0) == 0);
    const auto id = add_circle(state, sketch, c, 2);
    const auto loops = find_loops(sketch);
    CHECK(loops.closed.size() == 1);
    CHECK(loops.closed[0].edges[0].curve == id);
}

TEST(sketch_removal_cleans_up_points) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    const auto corner = sketch.lines[0].start;
    CHECK(remove_entity(sketch, sketch.lines[2].id));
    CHECK(sketch.lines.size() == 3);
    CHECK(sketch.points.size() == 4);
    // Removing a corner removes both lines through it and the orphaned point.
    CHECK(remove_entity(sketch, corner));
    CHECK(sketch.lines.size() == 1);
    CHECK(sketch.points.size() == 2);
    CHECK(!remove_entity(sketch, 9999));
}

TEST(sketch_edits_undo_through_the_document) {
    Document document;
    ObjectId sketch_id = 0;
    CHECK(document.edit("New sketch", [&](DocumentState& s) { sketch_id = add_sketch(s, SketchPlane::xy); }));
    CHECK(document.edit("Rectangle", [&](DocumentState& s) {
        static_cast<void>(add_rectangle(s, *s.sketch(sketch_id), {0, 0}, {5, 5}, 1e-6));
    }));
    CHECK(document.state().sketch(sketch_id)->lines.size() == 4);
    CHECK(document.undo());
    CHECK(document.state().sketch(sketch_id)->lines.empty());
    CHECK(document.undo());
    CHECK(document.state().sketches.empty());
}
