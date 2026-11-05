#include "features/regenerate.hpp"

#include "model/sketch_edit.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace studio::features {

namespace {
using model::Vec2;

constexpr double tau = 2 * std::numbers::pi;

struct Ring {
    kernel::Loop loop;
    std::vector<Vec2> polygon; // sampled boundary, for nesting
    double area = 0;           // unsigned
};

void sample_arc(std::vector<Vec2>& out, Vec2 center, double radius, double from, double to) {
    constexpr int steps = 32;
    for (int i = 0; i < steps; ++i) {
        const double a = from + (to - from) * i / steps;
        out.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
}

Ring make_ring(const model::Sketch& sketch, const model::SketchLoop& loop) {
    Ring ring;
    for (const auto& edge : loop.edges) {
        if (const auto* line = sketch.line(edge.curve)) {
            auto a = sketch.at(line->start);
            auto b = sketch.at(line->end);
            if (edge.reversed) { std::swap(a, b); }
            ring.loop.segments.emplace_back(kernel::Line{{a.x, a.y}, {b.x, b.y}});
            ring.polygon.push_back(a);
        } else if (const auto* arc = sketch.arc(edge.curve)) {
            const auto c = sketch.at(arc->center);
            const auto s = sketch.at(arc->start);
            const auto e = sketch.at(arc->end);
            const double radius = model::distance(c, s);
            const double from = std::atan2(s.y - c.y, s.x - c.x);
            double to = std::atan2(e.y - c.y, e.x - c.x);
            ring.loop.segments.emplace_back(kernel::Arc{{c.x, c.y}, radius, from, to, edge.reversed});
            if (to <= from) { to += tau; }
            if (edge.reversed) {
                sample_arc(ring.polygon, c, radius, to, from);
            } else {
                sample_arc(ring.polygon, c, radius, from, to);
            }
        } else if (const auto* circle = sketch.circle(edge.curve)) {
            const auto c = sketch.at(circle->center);
            ring.loop.segments.emplace_back(kernel::Circle{{c.x, c.y}, circle->radius});
            sample_arc(ring.polygon, c, circle->radius, 0, tau);
        }
    }
    double twice = 0;
    for (std::size_t i = 0; i < ring.polygon.size(); ++i) {
        const auto a = ring.polygon[i];
        const auto b = ring.polygon[(i + 1) % ring.polygon.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    ring.area = std::abs(twice) / 2;
    return ring;
}

bool inside(const std::vector<Vec2>& polygon, Vec2 p) {
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto a = polygon[i];
        const auto b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) { in = !in; }
    }
    return in;
}

void append(kernel::Mesh& mesh, const kernel::Mesh& part) {
    const auto base = static_cast<std::uint32_t>(mesh.positions.size() / 3);
    mesh.positions.insert(mesh.positions.end(), part.positions.begin(), part.positions.end());
    mesh.normals.insert(mesh.normals.end(), part.normals.begin(), part.normals.end());
    for (const auto index : part.indices) { mesh.indices.push_back(base + index); }
}
} // namespace

kernel::Plane sketch_plane(model::SketchPlane plane) noexcept {
    switch (plane) {
    case model::SketchPlane::xz: return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    case model::SketchPlane::yz: return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case model::SketchPlane::xy: break;
    }
    return {};
}

std::vector<kernel::Profile> sketch_profiles(const model::Sketch& sketch) {
    std::vector<Ring> rings;
    for (const auto& loop : model::find_loops(sketch).closed) {
        auto ring = make_ring(sketch, loop);
        if (!ring.polygon.empty()) { rings.push_back(std::move(ring)); }
    }
    // Depth = how many other loops enclose this one; the innermost encloser
    // is the parent. A loop is enclosed when all of its samples lie inside;
    // loops that are partly inside cross, which no profile can represent.
    const auto encloses = [&](const Ring& outer, const Ring& ring) {
        const auto count = std::ranges::count_if(ring.polygon, [&](const auto& p) { return inside(outer.polygon, p); });
        if (count != 0 && static_cast<std::size_t>(count) != ring.polygon.size()) {
            throw kernel::KernelError("the sketch's closed profiles cross each other");
        }
        return count != 0;
    };
    std::vector<std::size_t> depth(rings.size(), 0);
    std::vector<std::ptrdiff_t> parent(rings.size(), -1);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        for (std::size_t j = 0; j < rings.size(); ++j) {
            if (i == j || rings[j].area <= rings[i].area || !encloses(rings[j], rings[i])) { continue; }
            ++depth[i];
            if (parent[i] < 0 || rings[j].area < rings[static_cast<std::size_t>(parent[i])].area) {
                parent[i] = static_cast<std::ptrdiff_t>(j);
            }
        }
    }
    std::vector<kernel::Profile> profiles;
    std::vector<std::ptrdiff_t> profile_of(rings.size(), -1);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        if (depth[i] % 2 == 0) {
            profile_of[i] = static_cast<std::ptrdiff_t>(profiles.size());
            profiles.push_back({rings[i].loop, {}});
        }
    }
    for (std::size_t i = 0; i < rings.size(); ++i) {
        if (depth[i] % 2 == 1) {
            const auto owner = parent[i] < 0 ? -1 : profile_of[static_cast<std::size_t>(parent[i])];
            if (owner < 0) { throw kernel::KernelError("the sketch's closed profiles cross each other"); }
            profiles[static_cast<std::size_t>(owner)].holes.push_back(rings[i].loop);
        }
    }
    return profiles;
}

const FeatureResult* Regeneration::result(model::ObjectId feature) const noexcept {
    for (const auto& r : features) {
        if (r.feature == feature) { return &r; }
    }
    return nullptr;
}

double Regeneration::volume() const {
    double total = 0;
    for (const auto& r : features) {
        if (!r.solid.empty()) { total += kernel::volume(r.solid); }
    }
    return total;
}

Regeneration regenerate(const model::DocumentState& state, double tolerance) {
    Regeneration out;
    for (const auto& extrude : state.extrudes) {
        FeatureResult result{.feature = extrude.id};
        try {
            const auto* sketch = state.sketch(extrude.sketch);
            if (!sketch) { throw kernel::KernelError("its sketch no longer exists"); }
            if (!std::isfinite(extrude.distance) || extrude.distance == 0) {
                throw kernel::KernelError("the distance must be a non-zero number");
            }
            const auto profiles = sketch_profiles(*sketch);
            if (profiles.empty()) { throw kernel::KernelError(sketch->name + " has no closed profile"); }
            result.solid = kernel::extrude(sketch_plane(sketch->plane), profiles, extrude.distance);
            append(out.mesh, kernel::tessellate(result.solid, tolerance));
        } catch (const std::exception& error) {
            result.solid = {};
            result.error = error.what();
        }
        out.features.push_back(std::move(result));
    }
    return out;
}

} // namespace studio::features
