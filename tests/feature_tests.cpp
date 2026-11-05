#include "features/regenerate.hpp"
#include "model/sketch_edit.hpp"
#include "model/solver.hpp"
#include "studio/cad/sketch_geometry.hpp"
#include "test.hpp"

#include <numbers>

using namespace studio::model;
namespace features = studio::features;

namespace {
ObjectId point(DocumentState& state, Sketch& sketch, Vec2 at) { return point_at(state, sketch, at, 1e-6); }
} // namespace

TEST(profiles_nest_holes_and_islands) {
    DocumentState state;
    auto& sketch = *state.sketch(add_sketch(state, SketchPlane::xy));
    CHECK(add_rectangle(state, sketch, {0, 0}, {40, 40}, 1e-6));
    CHECK(add_circle(state, sketch, point(state, sketch, {20, 20}), 10) != 0); // hole
    CHECK(add_circle(state, sketch, point(state, sketch, {20, 20.5}), 4) != 0); // island in the hole
    CHECK(add_circle(state, sketch, point(state, sketch, {100, 0}), 5) != 0);  // separate body
    const auto profiles = features::sketch_profiles(sketch);
    CHECK(profiles.size() == 3);
    std::size_t holes = 0;
    for (const auto& p : profiles) { holes += p.holes.size(); }
    CHECK(holes == 1);
    add_extrude(state, sketch.id, 2);
    const auto model = features::regenerate(state);
    CHECK(model.features.size() == 1 && model.features[0].error.empty());
    const double pi = std::numbers::pi;
    CHECK_NEAR(model.volume(), (1600 - pi * 100 + pi * 16 + pi * 25) * 2);
    CHECK(!model.mesh.indices.empty());
}

TEST(profiles_accept_arcs_walked_in_reverse) {
    // A slot whose loop walks its right arc from end to start.
    DocumentState state;
    auto& sketch = *state.sketch(add_sketch(state, SketchPlane::xy));
    const auto a = point(state, sketch, {0, 0});
    const auto b = point(state, sketch, {20, 0});
    const auto c = point(state, sketch, {20, 10});
    const auto d = point(state, sketch, {0, 10});
    CHECK(add_line(state, sketch, a, b) != 0);
    CHECK(add_arc(state, sketch, point(state, sketch, {20, 5}), b, c) != 0);
    CHECK(add_line(state, sketch, c, d) != 0);
    CHECK(add_arc(state, sketch, point(state, sketch, {0, 5}), d, a) != 0);
    add_extrude(state, sketch.id, 1);
    const auto model = features::regenerate(state);
    CHECK(model.features[0].error.empty());
    CHECK_NEAR(model.volume(), 200 + std::numbers::pi * 25);
}

TEST(sketch_planes_agree_between_view_and_kernel) {
    for (const auto plane : {SketchPlane::xy, SketchPlane::xz, SketchPlane::yz}) {
        const auto k = features::sketch_plane(plane);
        const auto v = studio::cad::on_plane(plane, {2, 3});
        CHECK_NEAR(v.x, k.x_axis.x * 2 + k.y_axis.x * 3);
        CHECK_NEAR(v.y, k.x_axis.y * 2 + k.y_axis.y * 3);
        CHECK_NEAR(v.z, k.x_axis.z * 2 + k.y_axis.z * 3);
    }
}

TEST(regeneration_follows_edits_and_undo) {
    Document document;
    ObjectId width = 0;
    ObjectId extrude = 0;
    CHECK(document.edit("Sketch", [&](DocumentState& state) {
        auto& sketch = *state.sketch(add_sketch(state, SketchPlane::xz));
        CHECK(add_rectangle(state, sketch, {0, 0}, {30, 20}, 1e-6));
        CHECK(constrain(state, sketch, ConstraintKind::fixed, sketch.points[0].id).id != 0);
        width = constrain(state, sketch, ConstraintKind::distance, sketch.lines[0].id).id;
        extrude = add_extrude(state, sketch.id, 10);
    }));
    auto model = features::regenerate(document.state());
    CHECK_NEAR(model.volume(), 6000.0);
    // XZ extrudes along its normal, -Y.
    CHECK_NEAR(studio::kernel::bounds(model.features[0].solid).min.y, -10.0);

    CHECK(document.edit("Width", [&](DocumentState& state) {
        CHECK(set_dimension(state.sketches[0], width, 45));
    }));
    CHECK_NEAR(features::regenerate(document.state()).volume(), 9000.0);
    CHECK(document.edit("Distance", [&](DocumentState& state) { state.extrude(extrude)->distance = -4; }));
    CHECK_NEAR(features::regenerate(document.state()).volume(), 3600.0);
    CHECK(document.undo());
    CHECK_NEAR(features::regenerate(document.state()).volume(), 9000.0);
    CHECK(document.undo());
    CHECK_NEAR(features::regenerate(document.state()).volume(), 6000.0);
}

TEST(regeneration_reports_failures_per_feature) {
    DocumentState state;
    auto& open = *state.sketch(add_sketch(state, SketchPlane::xy));
    CHECK(add_line(state, open, point(state, open, {0, 0}), point(state, open, {10, 0})) != 0);
    const auto bad = add_extrude(state, open.id, 5);
    auto& good = *state.sketch(add_sketch(state, SketchPlane::xy));
    CHECK(add_rectangle(state, good, {0, 0}, {10, 10}, 1e-6));
    const auto ok = add_extrude(state, good.id, 1);
    const auto zero = add_extrude(state, good.id, 0);
    const auto orphan = add_extrude(state, 9999, 1);

    const auto model = features::regenerate(state);
    CHECK(model.result(bad)->error.find("no closed profile") != std::string::npos);
    CHECK(model.result(ok)->error.empty());
    CHECK(!model.result(zero)->error.empty());
    CHECK(!model.result(orphan)->error.empty());
    CHECK_NEAR(model.volume(), 100.0);
    CHECK(!model.mesh.indices.empty());
}
