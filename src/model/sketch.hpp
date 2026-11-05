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

enum class ConstraintKind : std::uint8_t {
    coincident,    // point on point, line, arc or circle
    horizontal,    // line
    vertical,      // line
    parallel,      // two lines
    perpendicular, // two lines
    equal,         // two lines (length) or two arcs/circles (radius)
    distance,      // `value` between two points, or the length of a line
    radius,        // `value` of an arc or circle
    diameter,      // `value` of an arc or circle
    fixed,         // point keeps its position
};

inline const char* constraint_name(ConstraintKind kind) noexcept {
    switch (kind) {
    case ConstraintKind::coincident: return "Coincident";
    case ConstraintKind::horizontal: return "Horizontal";
    case ConstraintKind::vertical: return "Vertical";
    case ConstraintKind::parallel: return "Parallel";
    case ConstraintKind::perpendicular: return "Perpendicular";
    case ConstraintKind::equal: return "Equal";
    case ConstraintKind::distance: return "Distance";
    case ConstraintKind::radius: return "Radius";
    case ConstraintKind::diameter: return "Diameter";
    case ConstraintKind::fixed: return "Fixed";
    }
    return "?";
}

// Dimensions carry a value the user can edit.
inline bool is_dimension(ConstraintKind kind) noexcept {
    return kind == ConstraintKind::distance || kind == ConstraintKind::radius ||
           kind == ConstraintKind::diameter;
}

struct SketchConstraint {
    ObjectId id = 0;
    ConstraintKind kind = ConstraintKind::fixed;
    ObjectId a = 0;
    ObjectId b = 0; // second entity, 0 when the constraint takes one
    double value = 0;
    bool operator==(const SketchConstraint&) const = default;
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
    std::vector<SketchConstraint> constraints;

    bool operator==(const Sketch&) const = default;

    template <typename T> static const T* find(const std::vector<T>& items, ObjectId id_) noexcept {
        for (const auto& item : items) {
            if (item.id == id_) { return &item; }
        }
        return nullptr;
    }
    const SketchLine* line(ObjectId id_) const noexcept { return find(lines, id_); }
    const SketchArc* arc(ObjectId id_) const noexcept { return find(arcs, id_); }
    const SketchCircle* circle(ObjectId id_) const noexcept { return find(circles, id_); }
    const SketchConstraint* constraint(ObjectId id_) const noexcept { return find(constraints, id_); }
    bool contains(ObjectId id_) const noexcept {
        return point(id_) || line(id_) || arc(id_) || circle(id_) || constraint(id_);
    }

    const SketchPoint* point(ObjectId id_) const noexcept { return find(points, id_); }
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
