#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

// Studio's geometry kernel interface. The implementation uses OpenCASCADE;
// no OpenCASCADE type appears here, so the kernel stays replaceable.
namespace studio::kernel {

struct Vec2 {
    double x = 0;
    double y = 0;
    bool operator==(const Vec2&) const = default;
};

struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;
    bool operator==(const Vec3&) const = default;
};

struct Line {
    Vec2 start;
    Vec2 end;
};

// Counter-clockwise arc from `start_angle` to `end_angle` (radians). A
// `reversed` arc is the same curve walked from its end back to its start,
// as a loop may traverse it.
struct Arc {
    Vec2 center;
    double radius = 0;
    double start_angle = 0;
    double end_angle = 0;
    bool reversed = false;
};

struct Circle {
    Vec2 center;
    double radius = 0;
};

using Segment = std::variant<Line, Arc, Circle>;

// A closed loop: connected segments, each starting where the previous one
// ends, or a single circle.
struct Loop {
    std::vector<Segment> segments;
};

// A region to extrude: one outer boundary and any holes inside it.
struct Profile {
    Loop outer;
    std::vector<Loop> holes;
};

// The plane a sketch lies on; sketch (x, y) maps to origin + x*x_axis + y*y_axis.
struct Plane {
    Vec3 origin{0, 0, 0};
    Vec3 x_axis{1, 0, 0};
    Vec3 y_axis{0, 1, 0};
};

struct Mesh {
    std::vector<float> positions; // xyz per vertex
    std::vector<float> normals;   // xyz per vertex
    std::vector<std::uint32_t> indices;
};

struct Bounds {
    Vec3 min;
    Vec3 max;
};

class KernelError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An immutable solid body. Cheap to copy.
class Solid {
public:
    struct Data;
    Solid() = default;
    explicit Solid(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
    bool empty() const noexcept { return !data_; }
    const Data& data() const;

private:
    std::shared_ptr<const Data> data_;
};

// Extrudes profiles on `plane` along its normal by `distance` (negative
// extrudes the other way) and unions the results. Throws KernelError.
Solid extrude(const Plane& plane, std::span<const Profile> profiles, double distance);

// Triangulates the solid's surface. `tolerance` is the maximum chordal
// deviation in model units.
Mesh tessellate(const Solid& solid, double tolerance);

double volume(const Solid& solid);
Bounds bounds(const Solid& solid);

} // namespace studio::kernel
