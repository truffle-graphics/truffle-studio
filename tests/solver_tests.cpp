#include "model/sketch_edit.hpp"
#include "model/solver.hpp"
#include "test.hpp"

#include <cmath>

using namespace studio::model;

namespace {

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

Sketch& new_sketch(DocumentState& state) {
    return *state.sketch(add_sketch(state, SketchPlane::xy));
}

ObjectId line(DocumentState& state, Sketch& sketch, Vec2 a, Vec2 b) {
    return add_line(state, sketch, point_at(state, sketch, a, 1e-6), point_at(state, sketch, b, 1e-6));
}

} // namespace

TEST(solver_free_sketch_is_under_constrained) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    line(state, sketch, {0, 0}, {10, 1});
    const auto result = analyse(sketch);
    CHECK(result.converged);
    CHECK(result.status == SolveStatus::under);
    CHECK(result.freedom == 4);
}

TEST(solver_horizontal_and_vertical) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto h = line(state, sketch, {0, 0}, {10, 1});
    const auto v = line(state, sketch, {20, 0}, {21, 10});
    CHECK(constrain(state, sketch, ConstraintKind::horizontal, h).id != 0);
    CHECK(constrain(state, sketch, ConstraintKind::vertical, v).id != 0);
    const auto* l = sketch.line(h);
    CHECK(near(sketch.at(l->start).y, sketch.at(l->end).y));
    l = sketch.line(v);
    CHECK(near(sketch.at(l->start).x, sketch.at(l->end).x));
    // Small changes: the lines stay near where they were drawn.
    CHECK(std::abs(sketch.at(sketch.line(h)->end).x - 10) < 1);
}

TEST(solver_rectangle_fully_constrained_by_dimensions) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    CHECK(analyse(sketch).freedom == 4);
    const auto corner = sketch.points[0].id;
    CHECK(constrain(state, sketch, ConstraintKind::fixed, corner).id != 0);
    const auto width = constrain(state, sketch, ConstraintKind::distance, sketch.lines[0].id).id;
    const auto height = constrain(state, sketch, ConstraintKind::distance, sketch.lines[1].id).id;
    CHECK(width != 0 && height != 0);
    CHECK(near(sketch.constraint(width)->value, 20));
    const auto full = analyse(sketch);
    CHECK(full.status == SolveStatus::full);
    CHECK(full.freedom == 0);

    CHECK(set_dimension(sketch, width, 35));
    const auto* l = sketch.line(sketch.lines[0].id);
    CHECK(near(distance(sketch.at(l->start), sketch.at(l->end)), 35));
    CHECK(near(sketch.at(corner).x, 0) && near(sketch.at(corner).y, 0));
    CHECK(!set_dimension(sketch, width, -1));
    CHECK(!set_dimension(sketch, corner, 5));
}

TEST(solver_rejects_over_constraint) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    const auto before = sketch;
    // The top edge is already horizontal.
    const auto again = constrain(state, sketch, ConstraintKind::horizontal, sketch.lines[0].id);
    CHECK(again.error == ConstrainError::over_constrained);
    CHECK(sketch == before);
    // Opposite edges are already parallel.
    CHECK(constrain(state, sketch, ConstraintKind::parallel, sketch.lines[0].id, sketch.lines[2].id).error ==
          ConstrainError::over_constrained);
    CHECK(constrain(state, sketch, ConstraintKind::horizontal, sketch.points[0].id).error ==
          ConstrainError::unsuitable);
}

TEST(solver_parallel_perpendicular_equal) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto a = line(state, sketch, {0, 0}, {10, 0});
    const auto b = line(state, sketch, {0, 5}, {8, 7});
    const auto c = line(state, sketch, {20, 0}, {22, 9});
    CHECK(constrain(state, sketch, ConstraintKind::parallel, a, b).id != 0);
    CHECK(constrain(state, sketch, ConstraintKind::perpendicular, a, c).id != 0);
    CHECK(constrain(state, sketch, ConstraintKind::equal, a, c).id != 0);
    const auto dir = [&](ObjectId id) {
        const auto* l = sketch.line(id);
        return sketch.at(l->end) - sketch.at(l->start);
    };
    const auto da = dir(a), db = dir(b), dc = dir(c);
    CHECK(near(da.x * db.y - da.y * db.x, 0));
    CHECK(near(da.x * dc.x + da.y * dc.y, 0));
    CHECK(near(length(da), length(dc)));
}

TEST(solver_coincident_and_circles) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto l = line(state, sketch, {0, 0}, {10, 0});
    const auto center = point_at(state, sketch, {20, 3}, 1e-6);
    const auto circle = add_circle(state, sketch, center, 4);
    const auto other = add_circle(state, sketch, point_at(state, sketch, {40, 0}, 1e-6), 2);
    CHECK(constrain(state, sketch, ConstraintKind::coincident, center, l).id != 0);
    {
        const auto p0 = sketch.at(sketch.line(l)->start);
        const auto d = sketch.at(sketch.line(l)->end) - p0;
        const auto q = sketch.at(center) - p0;
        CHECK(near(d.x * q.y - d.y * q.x, 0));
    }
    CHECK(constrain(state, sketch, ConstraintKind::diameter, circle, 0, 12).id != 0);
    CHECK(near(sketch.circle(circle)->radius, 6));
    CHECK(constrain(state, sketch, ConstraintKind::equal, circle, other).id != 0);
    CHECK(near(sketch.circle(other)->radius, 6));
    const auto end = sketch.line(l)->end;
    CHECK(constrain(state, sketch, ConstraintKind::coincident, end, circle).id != 0);
    CHECK(near(distance(sketch.at(end), sketch.at(center)), 6));
}

TEST(solver_arc_keeps_its_radius) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    const auto c = point_at(state, sketch, {0, 0}, 1e-6);
    const auto s = point_at(state, sketch, {5, 0}, 1e-6);
    const auto e = point_at(state, sketch, {0, 7}, 1e-6); // off the circle
    const auto arc = add_arc(state, sketch, c, s, e);
    CHECK(constrain(state, sketch, ConstraintKind::radius, arc, 0, 5).id != 0);
    CHECK(near(distance(sketch.at(c), sketch.at(e)), 5));
    CHECK(near(distance(sketch.at(c), sketch.at(s)), 5));
}

TEST(solver_pinned_points_stay) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    const auto dragged = sketch.points[2].id;
    sketch.points[2].position = {30, 15};
    const ObjectId pinned[] = {dragged};
    CHECK(solve(sketch, pinned).converged);
    CHECK(near(sketch.at(dragged).x, 30) && near(sketch.at(dragged).y, 15));
    CHECK(find_loops(sketch).closed.size() == 1);
    for (const auto& c : sketch.constraints) {
        const auto* l = sketch.line(c.a);
        const auto d = sketch.at(l->end) - sketch.at(l->start);
        CHECK(near(c.kind == ConstraintKind::horizontal ? d.y : d.x, 0));
    }
}

TEST(solver_removing_entities_drops_constraints) {
    DocumentState state;
    auto& sketch = new_sketch(state);
    CHECK(add_rectangle(state, sketch, {0, 0}, {20, 10}, 1e-6));
    CHECK(sketch.constraints.size() == 4);
    CHECK(remove_entity(sketch, sketch.lines[0].id));
    CHECK(sketch.constraints.size() == 3);
    CHECK(remove_entity(sketch, sketch.constraints[0].id));
    CHECK(sketch.constraints.size() == 2);
}
