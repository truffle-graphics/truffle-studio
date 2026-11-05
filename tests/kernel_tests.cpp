#include "kernel/kernel.hpp"
#include "test.hpp"

#include <numbers>

namespace k = studio::kernel;

namespace {
k::Profile rectangle(double w, double h) {
    return {.outer = {{k::Line{{0, 0}, {w, 0}}, k::Line{{w, 0}, {w, h}},
                       k::Line{{w, h}, {0, h}}, k::Line{{0, h}, {0, 0}}}}};
}

// Every triangle normal must agree with the averaged vertex normals, i.e.
// faces are wound outward.
bool outward(const k::Mesh& mesh) {
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const auto p = [&](std::size_t i, std::size_t c) { return mesh.positions[mesh.indices[t + i] * 3 + c]; };
        const auto n = [&](std::size_t i, std::size_t c) { return mesh.normals[mesh.indices[t + i] * 3 + c]; };
        const float ux = p(1, 0) - p(0, 0), uy = p(1, 1) - p(0, 1), uz = p(1, 2) - p(0, 2);
        const float vx = p(2, 0) - p(0, 0), vy = p(2, 1) - p(0, 1), vz = p(2, 2) - p(0, 2);
        const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        const float dot = cx * (n(0, 0) + n(1, 0) + n(2, 0)) + cy * (n(0, 1) + n(1, 1) + n(2, 1)) +
                          cz * (n(0, 2) + n(1, 2) + n(2, 2));
        if (dot <= 0) { return false; }
    }
    return true;
}
} // namespace

TEST(kernel_extrudes_rectangle) {
    const auto profile = rectangle(40, 20);
    const auto solid = k::extrude({}, {&profile, 1}, 10);
    CHECK_NEAR(k::volume(solid), 8000.0);
    const auto box = k::bounds(solid);
    CHECK_NEAR(box.max.x - box.min.x, 40.0);
    CHECK_NEAR(box.max.z - box.min.z, 10.0);
    const auto mesh = k::tessellate(solid, 0.1);
    CHECK(mesh.indices.size() == 12 * 3);
    CHECK(mesh.normals.size() == mesh.positions.size());
    CHECK(outward(mesh));
}

TEST(kernel_accepts_clockwise_profiles_and_negative_distance) {
    k::Profile profile{.outer = {{k::Line{{0, 0}, {0, 10}}, k::Line{{0, 10}, {10, 10}},
                                  k::Line{{10, 10}, {10, 0}}, k::Line{{10, 0}, {0, 0}}}}};
    const auto solid = k::extrude({}, {&profile, 1}, -5);
    CHECK_NEAR(k::volume(solid), 500.0);
    CHECK_NEAR(k::bounds(solid).min.z, -5.0);
    CHECK(outward(k::tessellate(solid, 0.1)));
}

TEST(kernel_extrudes_circle_with_tolerance) {
    k::Profile profile{.outer = {{k::Circle{{0, 0}, 5}}}};
    const auto solid = k::extrude({}, {&profile, 1}, 2);
    CHECK_NEAR(k::volume(solid), std::numbers::pi * 25 * 2);
    const auto coarse = k::tessellate(solid, 0.5);
    const auto fine = k::tessellate(k::extrude({}, {&profile, 1}, 2), 0.01);
    CHECK(fine.indices.size() > coarse.indices.size());
    CHECK(outward(fine));
}

TEST(kernel_extrudes_profile_with_hole_and_arc) {
    // A 20x10 slot: two lines and two semicircular ends, with a round hole.
    k::Profile profile{.outer = {{k::Line{{0, 0}, {20, 0}},
                                  k::Arc{{20, 5}, 5, -std::numbers::pi / 2, std::numbers::pi / 2},
                                  k::Line{{20, 10}, {0, 10}},
                                  k::Arc{{0, 5}, 5, std::numbers::pi / 2, 3 * std::numbers::pi / 2}}},
                       .holes = {{{k::Circle{{10, 5}, 2}}}}};
    const auto solid = k::extrude({}, {&profile, 1}, 1);
    const double expected = 20 * 10 + std::numbers::pi * 25 - std::numbers::pi * 4;
    CHECK_NEAR(k::volume(solid), expected);
}

TEST(kernel_uses_plane_frame) {
    const auto profile = rectangle(2, 3);
    const k::Plane side{.origin = {0, 0, 5}, .x_axis = {0, 1, 0}, .y_axis = {0, 0, 1}};
    const auto box = k::bounds(k::extrude(side, {&profile, 1}, 4));
    CHECK_NEAR(box.min.x, 0.0);
    CHECK_NEAR(box.max.x, 4.0);
    CHECK_NEAR(box.max.y - box.min.y, 2.0);
    CHECK_NEAR(box.min.z, 5.0);
}

TEST(kernel_rejects_open_profiles) {
    k::Profile open{.outer = {{k::Line{{0, 0}, {1, 0}}, k::Line{{1, 0}, {1, 1}}}}};
    bool threw = false;
    try {
        static_cast<void>(k::extrude({}, {&open, 1}, 1));
    } catch (const k::KernelError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(kernel_unites_profiles) {
    const k::Profile profiles[] = {rectangle(10, 10),
                                   {.outer = {{k::Circle{{30, 5}, 1}}}}};
    CHECK_NEAR(k::volume(k::extrude({}, profiles, 1)), 100 + std::numbers::pi);
}
