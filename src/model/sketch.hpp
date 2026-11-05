#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace studio::model {

// Stable identity for document objects; 0 is never assigned.
using ObjectId = std::uint64_t;

// Sketch coordinates are millimetres on the sketch plane.
struct Vec2 {
    double x = 0;
    double y = 0;

    friend Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend Vec2 operator*(Vec2 a, double s) noexcept { return {a.x * s, a.y * s}; }
    bool operator==(const Vec2&) const = default;
};

inline double length(Vec2 v) noexcept { return std::hypot(v.x, v.y); }
inline double distance(Vec2 a, Vec2 b) noexcept { return length(b - a); }

// The model plane a sketch lies on. The sketch's x and y axes map to the
// first and second named axes.
enum class SketchPlane : std::uint8_t { xy, xz, yz };

inline const char* plane_name(SketchPlane plane) noexcept {
    switch (plane) {
    case SketchPlane::xy: return "XY";
    case SketchPlane::xz: return "XZ";
    case SketchPlane::yz: return "YZ";
    }
    return "?";
}

struct SketchPoint {
    ObjectId id = 0;
    Vec2 position;
    bool operator==(const SketchPoint&) const = default;
};

struct SketchLine {
    ObjectId id = 0;
    ObjectId start = 0;
    ObjectId end = 0;
    bool operator==(const SketchLine&) const = default;
};

// Counter-clockwise from `start` to `end` around `center`. The radius is the
// distance from the centre to `start`; `end` gives only the direction.
struct SketchArc {
    ObjectId id = 0;
    ObjectId center = 0;
    ObjectId start = 0;
    ObjectId end = 0;
    bool operator==(const SketchArc&) const = default;
};

struct SketchCircle {
    ObjectId id = 0;
    ObjectId center = 0;
    double radius = 0;
    bool operator==(const SketchCircle&) const = default;
};

// A 2D sketch. Curves share endpoints by point id, so moving one point moves
// every curve that uses it.
struct Sketch {
    ObjectId id = 0;
    std::string name;
    SketchPlane plane = SketchPlane::xy;
    std::vector<SketchPoint> points;
    std::vector<SketchLine> lines;
    std::vector<SketchArc> arcs;
    std::vector<SketchCircle> circles;

    bool operator==(const Sketch&) const = default;

    const SketchPoint* point(ObjectId id_) const noexcept {
        for (const auto& p : points) {
            if (p.id == id_) { return &p; }
        }
        return nullptr;
    }
    SketchPoint* point(ObjectId id_) noexcept {
        return const_cast<SketchPoint*>(static_cast<const Sketch&>(*this).point(id_));
    }
    Vec2 at(ObjectId id_) const noexcept {
        const auto* p = point(id_);
        return p ? p->position : Vec2{};
    }
    std::size_t curve_count() const noexcept { return lines.size() + arcs.size() + circles.size(); }
};

} // namespace studio::model
