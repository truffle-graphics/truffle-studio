#pragma once

#include "model/sketch.hpp"
#include "studio/cad/orbit_camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

// Sketch geometry shared by the 2D editor, the 3D view and regeneration.
namespace studio::cad {

// Polyline through a counter-clockwise arc from `start` around `center` to
// the angle of `end`.
inline std::vector<model::Vec2> arc_points(model::Vec2 center, model::Vec2 start, model::Vec2 end) {
    constexpr double tau = 2 * std::numbers::pi;
    const double radius = model::distance(center, start);
    const double from = std::atan2(start.y - center.y, start.x - center.x);
    double to = std::atan2(end.y - center.y, end.x - center.x);
    if (to <= from) { to += tau; }
    const auto steps = std::max(8, static_cast<int>((to - from) / tau * 96));
    std::vector<model::Vec2> points;
    for (int i = 0; i <= steps; ++i) {
        const double a = from + (to - from) * i / steps;
        points.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    return points;
}

inline std::vector<model::Vec2> circle_points(model::Vec2 center, double radius) {
    constexpr double tau = 2 * std::numbers::pi;
    std::vector<model::Vec2> points;
    for (int i = 0; i <= 96; ++i) {
        const double a = tau * i / 96;
        points.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    return points;
}

// The model axes a sketch's x and y map to; every plane passes through the
// origin and Z is up.
struct PlaneAxes {
    Vec3 x;
    Vec3 y;
};

inline PlaneAxes plane_axes(model::SketchPlane plane) noexcept {
    switch (plane) {
    case model::SketchPlane::xz: return {{1, 0, 0}, {0, 0, 1}};
    case model::SketchPlane::yz: return {{0, 1, 0}, {0, 0, 1}};
    case model::SketchPlane::xy: break;
    }
    return {{1, 0, 0}, {0, 1, 0}};
}

inline Vec3 on_plane(model::SketchPlane plane, model::Vec2 point) noexcept {
    const auto axes = plane_axes(plane);
    return axes.x * point.x + axes.y * point.y;
}

// Every curve of the sketch as a polyline in sketch coordinates.
inline std::vector<std::vector<model::Vec2>> sketch_polylines(const model::Sketch& sketch) {
    std::vector<std::vector<model::Vec2>> lines;
    for (const auto& l : sketch.lines) { lines.push_back({sketch.at(l.start), sketch.at(l.end)}); }
    for (const auto& a : sketch.arcs) { lines.push_back(arc_points(sketch.at(a.center), sketch.at(a.start), sketch.at(a.end))); }
    for (const auto& c : sketch.circles) { lines.push_back(circle_points(sketch.at(c.center), c.radius)); }
    return lines;
}

} // namespace studio::cad
