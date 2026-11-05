#pragma once

#include "model/document.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace studio::model {

// Adds an empty sketch with a unique "Sketch N" name.
inline ObjectId add_sketch(DocumentState& state, SketchPlane plane) {
    std::size_t number = state.sketches.size() + 1;
    const auto taken = [&](const std::string& name) {
        return std::ranges::any_of(state.sketches, [&](const Sketch& s) { return s.name == name; });
    };
    while (taken("Sketch " + std::to_string(number))) { ++number; }
    Sketch sketch;
    sketch.id = Document::allocate(state);
    sketch.name = "Sketch " + std::to_string(number);
    sketch.plane = plane;
    state.sketches.push_back(std::move(sketch));
    return state.sketches.back().id;
}

// The point nearest `position` within `tolerance`, or 0.
inline ObjectId find_point(const Sketch& sketch, Vec2 position, double tolerance) {
    ObjectId found = 0;
    double best = tolerance;
    for (const auto& p : sketch.points) {
        const double d = distance(p.position, position);
        if (d <= best) {
            best = d;
            found = p.id;
        }
    }
    return found;
}

// Reuses the point within `tolerance` of `position` or adds one, so curves
// drawn from a shared point stay connected.
inline ObjectId point_at(DocumentState& state, Sketch& sketch, Vec2 position, double tolerance) {
    if (const auto existing = find_point(sketch, position, tolerance)) { return existing; }
    sketch.points.push_back({Document::allocate(state), position});
    return sketch.points.back().id;
}

// Returns 0 (and adds nothing) for a zero-length or duplicate line.
inline ObjectId add_line(DocumentState& state, Sketch& sketch, ObjectId start, ObjectId end) {
    if (start == end || !sketch.point(start) || !sketch.point(end)) { return 0; }
    for (const auto& line : sketch.lines) {
        if ((line.start == start && line.end == end) || (line.start == end && line.end == start)) {
            return 0;
        }
    }
    sketch.lines.push_back({Document::allocate(state), start, end});
    return sketch.lines.back().id;
}

// Returns 0 for a degenerate arc (zero radius or coincident ends).
inline ObjectId add_arc(DocumentState& state, Sketch& sketch, ObjectId center, ObjectId start,
                        ObjectId end) {
    if (start == end || center == start || center == end) { return 0; }
    if (!sketch.point(center) || !sketch.point(start) || !sketch.point(end)) { return 0; }
    sketch.arcs.push_back({Document::allocate(state), center, start, end});
    return sketch.arcs.back().id;
}

inline ObjectId add_circle(DocumentState& state, Sketch& sketch, ObjectId center, double radius) {
    if (!(radius > 0) || !sketch.point(center)) { return 0; }
    sketch.circles.push_back({Document::allocate(state), center, radius});
    return sketch.circles.back().id;
}

// Four connected lines through opposite corners `a` and `b`, kept
// horizontal and vertical by constraints. Returns false if it has no area.
inline bool add_rectangle(DocumentState& state, Sketch& sketch, Vec2 a, Vec2 b, double tolerance) {
    if (std::abs(a.x - b.x) <= tolerance || std::abs(a.y - b.y) <= tolerance) { return false; }
    const ObjectId corners[] = {point_at(state, sketch, a, tolerance),
                                point_at(state, sketch, {b.x, a.y}, tolerance),
                                point_at(state, sketch, b, tolerance),
                                point_at(state, sketch, {a.x, b.y}, tolerance)};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto line = add_line(state, sketch, corners[i], corners[(i + 1) % 4]);
        if (line) {
            const auto kind = i % 2 == 0 ? ConstraintKind::horizontal : ConstraintKind::vertical;
            sketch.constraints.push_back({Document::allocate(state), kind, line, 0, 0});
        }
    }
    return true;
}

// Removes points that no curve uses.
inline void prune_points(Sketch& sketch) {
    std::erase_if(sketch.points, [&](const SketchPoint& p) {
        const auto uses = [&](ObjectId id) { return id == p.id; };
        return std::ranges::none_of(sketch.lines, [&](const auto& c) { return uses(c.start) || uses(c.end); }) &&
               std::ranges::none_of(sketch.arcs, [&](const auto& c) {
                   return uses(c.center) || uses(c.start) || uses(c.end);
               }) &&
               std::ranges::none_of(sketch.circles, [&](const auto& c) { return uses(c.center); });
    });
}

// Removes a constraint; or a curve, or a point together with every curve
// using it, then any points left unused and the constraints on removed
// entities. Returns false if `id` is not in the sketch.
inline bool remove_entity(Sketch& sketch, ObjectId id) {
    if (sketch.constraint(id)) {
        std::erase_if(sketch.constraints, [&](const auto& c) { return c.id == id; });
        return true;
    }
    const auto before = sketch.curve_count() + sketch.points.size();
    const bool is_point = sketch.point(id) != nullptr;
    std::erase_if(sketch.lines, [&](const auto& c) {
        return c.id == id || (is_point && (c.start == id || c.end == id));
    });
    std::erase_if(sketch.arcs, [&](const auto& c) {
        return c.id == id || (is_point && (c.center == id || c.start == id || c.end == id));
    });
    std::erase_if(sketch.circles, [&](const auto& c) { return c.id == id || (is_point && c.center == id); });
    if (is_point) {
        std::erase_if(sketch.points, [&](const auto& p) { return p.id == id; });
    }
    prune_points(sketch);
    std::erase_if(sketch.constraints, [&](const SketchConstraint& c) {
        return !sketch.contains(c.a) || (c.b != 0 && !sketch.contains(c.b));
    });
    return sketch.curve_count() + sketch.points.size() != before;
}

// One curve of a loop, walked from start to end unless `reversed`.
struct LoopEdge {
    ObjectId curve = 0;
    bool reversed = false;
    bool operator==(const LoopEdge&) const = default;
};

struct SketchLoop {
    std::vector<LoopEdge> edges; // a circle is a loop of one edge
};

struct SketchLoops {
    std::vector<SketchLoop> closed;
    std::size_t open = 0; // connected chains that are not simple closed loops
};

// Finds the simple closed loops formed by lines, arcs and circles. A chain
// is closed when each of its endpoints joins exactly two curves; branching
// or dangling chains count as open.
inline SketchLoops find_loops(const Sketch& sketch) {
    struct Curve {
        ObjectId id;
        ObjectId start;
        ObjectId end;
    };
    std::vector<Curve> curves;
    for (const auto& l : sketch.lines) { curves.push_back({l.id, l.start, l.end}); }
    for (const auto& a : sketch.arcs) { curves.push_back({a.id, a.start, a.end}); }

    std::map<ObjectId, std::vector<std::size_t>> incident;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        incident[curves[i].start].push_back(i);
        incident[curves[i].end].push_back(i);
    }

    SketchLoops result;
    std::vector<bool> visited(curves.size(), false);
    for (std::size_t first = 0; first < curves.size(); ++first) {
        if (visited[first]) { continue; }
        // Collect the connected component.
        std::vector<std::size_t> component;
        std::vector<std::size_t> stack{first};
        visited[first] = true;
        while (!stack.empty()) {
            const auto i = stack.back();
            stack.pop_back();
            component.push_back(i);
            for (const auto point : {curves[i].start, curves[i].end}) {
                for (const auto j : incident[point]) {
                    if (!visited[j]) {
                        visited[j] = true;
                        stack.push_back(j);
                    }
                }
            }
        }
        const bool simple = std::ranges::all_of(component, [&](std::size_t i) {
            return incident[curves[i].start].size() == 2 && incident[curves[i].end].size() == 2;
        });
        if (!simple) {
            ++result.open;
            continue;
        }
        // Walk the cycle from the first curve.
        SketchLoop loop;
        std::size_t current = first;
        ObjectId at = curves[first].end;
        loop.edges.push_back({curves[first].id, false});
        while (true) {
            const auto& next = incident[at];
            const auto following = next[0] == current ? next[1] : next[0];
            if (following == first) { break; }
            const bool reversed = curves[following].end == at;
            loop.edges.push_back({curves[following].id, reversed});
            at = reversed ? curves[following].start : curves[following].end;
            current = following;
        }
        result.closed.push_back(std::move(loop));
    }
    for (const auto& c : sketch.circles) { result.closed.push_back({{{c.id, false}}}); }
    return result;
}

} // namespace studio::model
