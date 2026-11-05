#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// The 3D viewport's camera: pure math, no GPU or GUI dependency. Z is up,
// as in most CAD tools. Matrices are column-major with Metal's [0, 1] depth.
namespace studio::cad {

struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;
    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
};

inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const double l = length(a);
    return l > 0 ? a * (1 / l) : a;
}

using Matrix = std::array<float, 16>; // column-major

// Orbits `target` at `distance`; yaw turns about Z, pitch tilts towards it.
class OrbitCamera {
public:
    static constexpr double field_of_view = 0.7853981633974483; // 45° vertical

    Vec3 target{0, 0, 0};
    double distance = 200;
    double yaw = -0.7853981633974483; // looking from +X -Y towards the origin
    double pitch = 0.5;

    Vec3 eye() const {
        return target + Vec3{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                             std::sin(pitch)} * distance;
    }
    Vec3 forward() const { return normalize(target - eye()); }
    Vec3 right() const { return normalize(cross(forward(), {0, 0, 1})); }
    Vec3 up() const { return cross(right(), forward()); }

    // Drag deltas in pixels.
    void orbit(double dx, double dy) {
        yaw -= dx * 0.008;
        pitch = std::clamp(pitch + dy * 0.008, -1.55, 1.55);
    }
    // Moves the target so the scene follows a drag of (dx, dy) pixels in a
    // view `height` pixels tall.
    void pan(double dx, double dy, double height) {
        const double units = 2 * distance * std::tan(field_of_view / 2) / std::max(height, 1.0);
        target = target - right() * (dx * units) + up() * (dy * units);
    }
    // `factor` < 1 moves closer.
    void zoom(double factor) { distance = std::clamp(distance * factor, 1e-3, 1e7); }

    // Frames the box `min`..`max` with a margin, keeping the view direction.
    void fit(Vec3 min, Vec3 max) {
        target = (min + max) * 0.5;
        const double radius = std::max(length(max - min) / 2, 1.0);
        distance = radius / std::sin(field_of_view / 2) * 1.15;
    }

    Matrix view() const {
        const auto e = eye();
        const auto f = forward();
        const auto s = right();
        const auto u = up();
        return {static_cast<float>(s.x), static_cast<float>(u.x), static_cast<float>(-f.x), 0,
                static_cast<float>(s.y), static_cast<float>(u.y), static_cast<float>(-f.y), 0,
                static_cast<float>(s.z), static_cast<float>(u.z), static_cast<float>(-f.z), 0,
                static_cast<float>(-dot(s, e)), static_cast<float>(-dot(u, e)),
                static_cast<float>(dot(f, e)), 1};
    }
    Matrix projection(double aspect) const {
        const double near = distance * 0.01;
        const double far = distance * 100;
        const double f = 1 / std::tan(field_of_view / 2);
        Matrix m{};
        m[0] = static_cast<float>(f / std::max(aspect, 1e-6));
        m[5] = static_cast<float>(f);
        m[10] = static_cast<float>(far / (near - far));
        m[11] = -1;
        m[14] = static_cast<float>(near * far / (near - far));
        return m;
    }
    Matrix view_projection(double aspect) const { return multiply(projection(aspect), view()); }

    // Pixel position (origin top-left) of `point` in a width x height view;
    // empty when it is behind the camera.
    std::optional<std::array<double, 2>> project(Vec3 point, double width, double height) const {
        const auto m = view_projection(width / std::max(height, 1.0));
        std::array<double, 4> clip{};
        for (std::size_t row = 0; row < 4; ++row) {
            clip[row] = m[row] * point.x + m[4 + row] * point.y + m[8 + row] * point.z + m[12 + row];
        }
        if (clip[3] <= 1e-9) { return std::nullopt; }
        return std::array{(clip[0] / clip[3] + 1) / 2 * width, (1 - clip[1] / clip[3]) / 2 * height};
    }

    static Matrix multiply(const Matrix& a, const Matrix& b) {
        Matrix out{};
        for (std::size_t col = 0; col < 4; ++col) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0;
                for (std::size_t k = 0; k < 4; ++k) { sum += a[k * 4 + row] * b[col * 4 + k]; }
                out[col * 4 + row] = sum;
            }
        }
        return out;
    }
};

} // namespace studio::cad
