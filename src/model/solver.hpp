#pragma once

#include "model/document.hpp"
#include "model/sketch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <span>
#include <vector>

namespace studio::model {

enum class SolveStatus : std::uint8_t { under, full, over };

struct SolveResult {
    bool converged = false;
    SolveStatus status = SolveStatus::under;
    std::size_t freedom = 0;   // degrees of freedom left
    std::size_t redundant = 0; // equations that add nothing (or conflict)
    double residual = 0;       // largest equation error at the end
};

namespace detail {

// The sketch as equations over unknowns: free point coordinates and circle
// radii. Fixed and pinned points are constants. Arcs add the implicit
// equation that their end lies on the circle through their start.
class SketchSystem {
public:
    SketchSystem(const Sketch& sketch, std::span<const ObjectId> pinned) : sketch_(sketch) {
        const auto is_fixed = [&](ObjectId id) {
            return std::ranges::find(pinned, id) != pinned.end() ||
                   std::ranges::any_of(sketch.constraints, [&](const SketchConstraint& c) {
                       return c.kind == ConstraintKind::fixed && c.a == id;
                   });
        };
        for (const auto& p : sketch.points) {
            if (is_fixed(p.id)) {
                points_[p.id] = -1;
            } else {
                points_[p.id] = static_cast<long>(initial.size());
                initial.push_back(p.position.x);
                initial.push_back(p.position.y);
            }
        }
        for (const auto& c : sketch.circles) {
            radii_[c.id] = initial.size();
            initial.push_back(c.radius);
        }
    }

    std::vector<double> initial;

    void residuals(const std::vector<double>& x, std::vector<double>& out) const {
        out.clear();
        for (const auto& a : sketch_.arcs) {
            out.push_back(distance(at(x, a.center), at(x, a.end)) - distance(at(x, a.center), at(x, a.start)));
        }
        for (const auto& c : sketch_.constraints) { constraint(x, c, out); }
    }

    void write(const std::vector<double>& x, Sketch& sketch) const {
        for (auto& p : sketch.points) { p.position = at(x, p.id); }
        for (auto& c : sketch.circles) { c.radius = x[radii_.at(c.id)]; }
    }

private:
    static constexpr double tiny = 1e-12;

    Vec2 at(const std::vector<double>& x, ObjectId point) const {
        const auto index = points_.at(point);
        if (index < 0) { return sketch_.at(point); }
        const auto i = static_cast<std::size_t>(index);
        return {x[i], x[i + 1]};
    }
    Vec2 direction(const std::vector<double>& x, ObjectId line) const {
        const auto* l = sketch_.line(line);
        return at(x, l->end) - at(x, l->start);
    }
    double radius(const std::vector<double>& x, ObjectId curve) const {
        if (const auto* a = sketch_.arc(curve)) { return distance(at(x, a->center), at(x, a->start)); }
        return x[radii_.at(curve)];
    }
    ObjectId center(ObjectId curve) const {
        if (const auto* a = sketch_.arc(curve)) { return a->center; }
        return sketch_.circle(curve)->center;
    }
    static double cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
    static double dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

    void constraint(const std::vector<double>& x, const SketchConstraint& c, std::vector<double>& out) const {
        switch (c.kind) {
        case ConstraintKind::coincident: {
            const auto p = at(x, c.a);
            if (sketch_.point(c.b)) {
                const auto q = at(x, c.b);
                out.push_back(p.x - q.x);
                out.push_back(p.y - q.y);
            } else if (const auto* l = sketch_.line(c.b)) {
                const auto d = direction(x, c.b);
                out.push_back(cross(p - at(x, l->start), d) / std::max(length(d), tiny));
            } else {
                out.push_back(distance(p, at(x, center(c.b))) - radius(x, c.b));
            }
            break;
        }
        case ConstraintKind::horizontal: out.push_back(direction(x, c.a).y); break;
        case ConstraintKind::vertical: out.push_back(direction(x, c.a).x); break;
        case ConstraintKind::parallel:
        case ConstraintKind::perpendicular: {
            const auto d1 = direction(x, c.a);
            const auto d2 = direction(x, c.b);
            const double scale = std::max(length(d1) * length(d2), tiny);
            out.push_back((c.kind == ConstraintKind::parallel ? cross(d1, d2) : dot(d1, d2)) / scale);
            break;
        }
        case ConstraintKind::equal:
            if (sketch_.line(c.a)) {
                out.push_back(length(direction(x, c.a)) - length(direction(x, c.b)));
            } else {
                out.push_back(radius(x, c.a) - radius(x, c.b));
            }
            break;
        case ConstraintKind::distance:
            if (sketch_.line(c.a)) {
                out.push_back(length(direction(x, c.a)) - c.value);
            } else {
                out.push_back(distance(at(x, c.a), at(x, c.b)) - c.value);
            }
            break;
        case ConstraintKind::radius: out.push_back(radius(x, c.a) - c.value); break;
        case ConstraintKind::diameter: out.push_back(2 * radius(x, c.a) - c.value); break;
        case ConstraintKind::fixed: break;
        }
    }

    const Sketch& sketch_;
    std::map<ObjectId, long> points_; // first unknown of each point, -1 when constant
    std::map<ObjectId, std::size_t> radii_;
};

// Solves the n x n system `a` * x = `b` in place (Gaussian elimination with
// partial pivoting). Returns false if it is singular.
inline bool solve_linear(std::vector<double>& a, std::vector<double>& b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r * n + col]) > std::abs(a[pivot * n + col])) { pivot = r; }
        }
        if (std::abs(a[pivot * n + col]) < 1e-300) { return false; }
        if (pivot != col) {
            for (std::size_t c = 0; c < n; ++c) { std::swap(a[col * n + c], a[pivot * n + c]); }
            std::swap(b[col], b[pivot]);
        }
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r * n + col] / a[col * n + col];
            if (f == 0) { continue; }
            for (std::size_t c = col; c < n; ++c) { a[r * n + c] -= f * a[col * n + c]; }
            b[r] -= f * b[col];
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t c = i + 1; c < n; ++c) { sum -= a[i * n + c] * b[c]; }
        b[i] = sum / a[i * n + i];
    }
    return true;
}

// Numerical rank of the m x n matrix `a` (full pivoting).
inline std::size_t rank(std::vector<double> a, std::size_t m, std::size_t n) {
    double largest = 0;
    for (const double v : a) { largest = std::max(largest, std::abs(v)); }
    const double tolerance = 1e-7 * std::max(1.0, largest);
    std::vector<bool> used(n, false);
    std::size_t result = 0;
    for (std::size_t row = 0; row < m; ++row) {
        std::size_t best = n;
        for (std::size_t c = 0; c < n; ++c) {
            if (!used[c] && std::abs(a[row * n + c]) > tolerance &&
                (best == n || std::abs(a[row * n + c]) > std::abs(a[row * n + best]))) {
                best = c;
            }
        }
        if (best == n) { continue; }
        used[best] = true;
        ++result;
        for (std::size_t r = row + 1; r < m; ++r) {
            const double f = a[r * n + best] / a[row * n + best];
            for (std::size_t c = 0; c < n; ++c) { a[r * n + c] -= f * a[row * n + c]; }
        }
    }
    return result;
}

} // namespace detail

// Moves the sketch's free points and radii to satisfy every constraint with
// the smallest change (damped Gauss-Newton, Levenberg-Marquardt). `pinned`
// points stay where they are, as if fixed. The sketch changes only when the
// solve converges. Over-constrained means conflicting or redundant.
inline SolveResult solve(Sketch& sketch, std::span<const ObjectId> pinned = {}) {
    constexpr double tolerance = 1e-9;
    const detail::SketchSystem system(sketch, pinned);
    auto x = system.initial;
    const auto n = x.size();
    std::vector<double> r;
    system.residuals(x, r);
    const auto m = r.size();
    const auto sum_squares = [](const std::vector<double>& v) {
        double s = 0;
        for (const double e : v) { s += e * e; }
        return s;
    };
    const auto worst = [](const std::vector<double>& v) {
        double w = 0;
        for (const double e : v) { w = std::max(w, std::abs(e)); }
        return w;
    };
    std::vector<double> jacobian(m * n);
    const auto differentiate = [&] {
        std::vector<double> plus;
        std::vector<double> minus;
        for (std::size_t j = 0; j < n; ++j) {
            const double h = 1e-7 * std::max(1.0, std::abs(x[j]));
            const double saved = x[j];
            x[j] = saved + h;
            system.residuals(x, plus);
            x[j] = saved - h;
            system.residuals(x, minus);
            x[j] = saved;
            for (std::size_t i = 0; i < m; ++i) { jacobian[i * n + j] = (plus[i] - minus[i]) / (2 * h); }
        }
    };

    double lambda = 1e-3;
    std::vector<double> trial;
    std::vector<double> trial_r;
    for (int iteration = 0; iteration < 200 && n > 0 && worst(r) > tolerance; ++iteration) {
        differentiate();
        std::vector<double> normal(n * n, 0);
        std::vector<double> step(n, 0);
        for (std::size_t i = 0; i < m; ++i) {
            for (std::size_t a = 0; a < n; ++a) {
                const double ja = jacobian[i * n + a];
                if (ja == 0) { continue; }
                step[a] -= ja * r[i];
                for (std::size_t b = 0; b < n; ++b) { normal[a * n + b] += ja * jacobian[i * n + b]; }
            }
        }
        for (std::size_t a = 0; a < n; ++a) { normal[a * n + a] += lambda; }
        if (!detail::solve_linear(normal, step, n)) { break; }
        trial = x;
        for (std::size_t a = 0; a < n; ++a) { trial[a] += step[a]; }
        system.residuals(trial, trial_r);
        if (sum_squares(trial_r) < sum_squares(r)) {
            x = trial;
            r = trial_r;
            lambda = std::max(lambda / 3, 1e-12);
        } else if ((lambda *= 4) > 1e10) {
            break;
        }
    }

    SolveResult result;
    result.residual = worst(r);
    result.converged = result.residual <= 1e-7;
    differentiate();
    const auto rank = detail::rank(jacobian, m, n);
    result.freedom = n - rank;
    result.redundant = m - rank;
    if (!result.converged || result.redundant > 0) {
        result.status = SolveStatus::over;
    } else {
        result.status = result.freedom == 0 ? SolveStatus::full : SolveStatus::under;
    }
    if (result.converged) { system.write(x, sketch); }
    return result;
}

// Solves a copy: how constrained the sketch is, without changing it.
inline SolveResult analyse(const Sketch& sketch) {
    auto copy = sketch;
    return solve(copy);
}

// Whether `kind` can apply to entities `a` and `b` (0 when unused).
inline bool constraint_fits(const Sketch& s, ConstraintKind kind, ObjectId a, ObjectId b) {
    const auto round = [&](ObjectId id) { return s.arc(id) || s.circle(id); };
    switch (kind) {
    case ConstraintKind::coincident:
        return s.point(a) && b != a && (s.point(b) || s.line(b) || round(b));
    case ConstraintKind::horizontal:
    case ConstraintKind::vertical: return s.line(a) && b == 0;
    case ConstraintKind::parallel:
    case ConstraintKind::perpendicular: return s.line(a) && s.line(b) && a != b;
    case ConstraintKind::equal: return a != b && ((s.line(a) && s.line(b)) || (round(a) && round(b)));
    case ConstraintKind::distance: return (s.line(a) && b == 0) || (s.point(a) && s.point(b) && a != b);
    case ConstraintKind::radius:
    case ConstraintKind::diameter: return round(a) && b == 0;
    case ConstraintKind::fixed: return s.point(a) && b == 0;
    }
    return false;
}

// The current value a dimension of `kind` would have.
inline double measure(const Sketch& s, ConstraintKind kind, ObjectId a, ObjectId b) {
    switch (kind) {
    case ConstraintKind::distance:
        if (const auto* l = s.line(a)) { return distance(s.at(l->start), s.at(l->end)); }
        return distance(s.at(a), s.at(b));
    case ConstraintKind::radius:
    case ConstraintKind::diameter: {
        const double r = s.circle(a) ? s.circle(a)->radius : distance(s.at(s.arc(a)->center), s.at(s.arc(a)->start));
        return kind == ConstraintKind::diameter ? 2 * r : r;
    }
    default: return 0;
    }
}

enum class ConstrainError : std::uint8_t { none, unsuitable, over_constrained };

struct ConstrainResult {
    ObjectId id = 0;
    ConstrainError error = ConstrainError::none;
};

// Adds a constraint and solves. Dimensions without a positive `value` take
// the current measurement. The sketch is unchanged if the entities do not
// fit the kind, or the result is over-constrained.
inline ConstrainResult constrain(DocumentState& state, Sketch& sketch, ConstraintKind kind, ObjectId a,
                                 ObjectId b = 0, double value = 0) {
    if (!constraint_fits(sketch, kind, a, b)) { return {0, ConstrainError::unsuitable}; }
    if (std::ranges::any_of(sketch.constraints, [&](const SketchConstraint& c) {
            return c.kind == kind && ((c.a == a && c.b == b) || (c.a == b && c.b == a));
        })) {
        return {0, ConstrainError::over_constrained};
    }
    if (is_dimension(kind) && !(value > 0)) { value = measure(sketch, kind, a, b); }
    auto candidate = sketch;
    const auto id = state.next_id;
    candidate.constraints.push_back({id, kind, a, b, is_dimension(kind) ? value : 0});
    if (solve(candidate).status == SolveStatus::over) { return {0, ConstrainError::over_constrained}; }
    static_cast<void>(Document::allocate(state));
    sketch = std::move(candidate);
    return {id, ConstrainError::none};
}

// Changes a dimension and solves. Returns false, leaving the sketch alone,
// if `id` is not a dimension, `value` is not positive or no solution exists.
inline bool set_dimension(Sketch& sketch, ObjectId id, double value) {
    const auto* c = sketch.constraint(id);
    if (!c || !is_dimension(c->kind) || !(value > 0)) { return false; }
    auto candidate = sketch;
    for (auto& k : candidate.constraints) {
        if (k.id == id) { k.value = value; }
    }
    if (!solve(candidate).converged) { return false; }
    sketch = std::move(candidate);
    return true;
}

} // namespace studio::model
