#include "studio/cad/orbit_camera.hpp"
#include "studio/cad/sketch_geometry.hpp"
#include "test.hpp"

using studio::cad::OrbitCamera;
using studio::cad::Vec3;
using studio::test::near;

TEST(camera_fit_centres_and_shows_bounds) {
    OrbitCamera camera;
    const Vec3 low{0, 0, 0};
    const Vec3 high{30, 20, 10};
    camera.fit(low, high);
    CHECK(near(camera.target.x, 15) && near(camera.target.y, 10) && near(camera.target.z, 5));
    const auto centre = camera.project(camera.target, 800, 600);
    CHECK(centre && near((*centre)[0], 400, 1e-3) && near((*centre)[1], 300, 1e-3));
    for (const double x : {low.x, high.x}) {
        for (const double y : {low.y, high.y}) {
            for (const double z : {low.z, high.z}) {
                const auto p = camera.project({x, y, z}, 800, 600);
                CHECK(p && (*p)[0] >= 0 && (*p)[0] <= 800 && (*p)[1] >= 0 && (*p)[1] <= 600);
            }
        }
    }
}

TEST(camera_projects_z_up_and_hides_points_behind) {
    OrbitCamera camera;
    const auto origin = camera.project({0, 0, 0}, 800, 600);
    const auto above = camera.project({0, 0, 10}, 800, 600);
    CHECK(origin && above && (*above)[1] < (*origin)[1]);
    CHECK(!camera.project(camera.eye() - camera.forward() * 10, 800, 600));
}

TEST(camera_orbit_keeps_distance_and_clamps_pitch) {
    OrbitCamera camera;
    camera.orbit(120, 40);
    CHECK(near(studio::cad::length(camera.eye() - camera.target), camera.distance));
    camera.orbit(0, 100000);
    CHECK(camera.pitch <= 1.55);
    camera.orbit(0, -200000);
    CHECK(camera.pitch >= -1.55);
}

TEST(camera_zoom_and_pan) {
    OrbitCamera camera;
    camera.zoom(0.5);
    CHECK(near(camera.distance, 100));
    const auto before = camera.project({0, 0, 0}, 800, 600);
    camera.pan(40, 0, 600);
    const auto after = camera.project({0, 0, 0}, 800, 600);
    // The scene follows the drag.
    CHECK(before && after && near((*after)[0] - (*before)[0], 40, 1e-2) && near((*after)[1], (*before)[1], 1e-2));
}

TEST(sketch_planes_map_to_model_axes) {
    using studio::model::SketchPlane;
    const auto xy = studio::cad::on_plane(SketchPlane::xy, {2, 3});
    const auto xz = studio::cad::on_plane(SketchPlane::xz, {2, 3});
    const auto yz = studio::cad::on_plane(SketchPlane::yz, {2, 3});
    CHECK(near(xy.x, 2) && near(xy.y, 3) && near(xy.z, 0));
    CHECK(near(xz.x, 2) && near(xz.y, 0) && near(xz.z, 3));
    CHECK(near(yz.x, 0) && near(yz.y, 2) && near(yz.z, 3));
}
