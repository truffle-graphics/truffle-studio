#include "studio/cad/sketch_editor.hpp"

#include "model/sketch_edit.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

namespace studio::cad {
namespace gui = truffle::gui;
using model::Vec2;

namespace {
constexpr double tau = 2 * std::numbers::pi;
constexpr double same_point = 1e-9; // drawn positions are snapped, so exact
constexpr float snap_pixels = 8;
constexpr gui::Color background{0.11F, 0.12F, 0.14F, 1};
constexpr gui::Color curve_color{0.86F, 0.88F, 0.92F, 1};
constexpr gui::Color accent{0.35F, 0.6F, 1, 1};
constexpr gui::Color preview{0.35F, 0.6F, 1, 0.7F};
constexpr gui::Color active_tool{0.24F, 0.45F, 0.85F, 1};

std::vector<Vec2> arc_points(Vec2 center, Vec2 start, Vec2 end) {
    const double radius = model::distance(center, start);
    const double from = std::atan2(start.y - center.y, start.x - center.x);
    double to = std::atan2(end.y - center.y, end.x - center.x);
    if (to <= from) { to += tau; }
    const auto steps = std::max(8, static_cast<int>((to - from) / tau * 96));
    std::vector<Vec2> points;
    for (int i = 0; i <= steps; ++i) {
        const double a = from + (to - from) * i / steps;
        points.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    return points;
}

std::vector<Vec2> circle_points(Vec2 center, double radius) {
    std::vector<Vec2> points;
    for (int i = 0; i <= 96; ++i) {
        const double a = tau * i / 96;
        points.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    return points;
}

double segment_distance(Vec2 p, Vec2 a, Vec2 b) {
    const auto ab = b - a;
    const double len2 = ab.x * ab.x + ab.y * ab.y;
    const double t = len2 > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0, 1.0) : 0;
    return model::distance(p, a + ab * t);
}

// Whether `p` lies within the counter-clockwise sweep of the arc.
bool in_sweep(Vec2 p, Vec2 center, Vec2 start, Vec2 end) {
    const auto angle = [&](Vec2 v) {
        const double a = std::atan2(v.y - center.y, v.x - center.x) - std::atan2(start.y - center.y, start.x - center.x);
        return a < 0 ? a + tau : a;
    };
    return angle(p) <= angle(end);
}
} // namespace

const char* SketchEditor::tool_name(Tool tool) noexcept {
    switch (tool) {
    case Tool::select: return "Select";
    case Tool::line: return "Line";
    case Tool::rectangle: return "Rectangle";
    case Tool::circle: return "Circle";
    case Tool::arc: return "Arc";
    }
    return "?";
}

void SketchEditor::build(WorkspaceServices& services, gui::ElementId parent) {
    services_ = &services;
    auto& context = services.ui.context;

    const auto bar = services.ui.row(parent, 4);
    for (const auto tool : tools) {
        const auto highlight = services.ui.row(bar, 0, {2, 2, 2, 2});
        tool_buttons_.emplace_back(highlight, services.ui.button(highlight, tool_name(tool), [this, tool] {
            set_tool(tool);
        }));
    }

    canvas_ = need(context.create(parent));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(canvas_, params));
    need(context.set_decoration(canvas_, {.fill = background}));
    need(context.set_shape_builder(canvas_, std::make_shared<gui::CanvasBuilder>(
        [this](gui::ShapeWriter& out, gui::Size size) { paint(out, size); })));
    need(context.set_interaction(canvas_, {.hit_test = true, .enabled = true, .focusable = true}));
    need(context.set_event_handler(canvas_, [this](gui::Context& ctx, gui::RoutedEvent& event) {
        handle(ctx, event);
    }));
    need(context.set_clips_children(parent, true));
    refresh_tools();
    sync();
}

void SketchEditor::teardown() {
    services_ = nullptr;
    canvas_ = {};
    tool_buttons_.clear();
    pending_.clear();
    hover_.reset();
    pan_from_.reset();
}

void SketchEditor::sync() {
    if (!services_) { return; }
    const auto& state = services_->document.state();
    if (!state.sketch(sketch_)) { sketch_ = state.sketches.empty() ? 0 : state.sketches.back().id; }
    const auto* sketch = active();
    const auto exists = [&](model::ObjectId id) {
        const auto has = [&](const auto& items) {
            return std::ranges::any_of(items, [&](const auto& item) { return item.id == id; });
        };
        return sketch && (has(sketch->points) || has(sketch->lines) || has(sketch->arcs) || has(sketch->circles));
    };
    if (selection_ && !exists(selection_)) { selection_ = 0; }
    redraw();
    notify();
}

void SketchEditor::set_tool(Tool tool) {
    tool_ = tool;
    cancel();
    refresh_tools();
    if (services_) { services_->report(std::string("Tool: ") + tool_name(tool)); }
    notify();
}

void SketchEditor::set_sketch(model::ObjectId sketch) {
    if (sketch == sketch_) { return; }
    sketch_ = sketch;
    selection_ = 0;
    cancel();
    redraw();
    notify();
}

gui::Widget SketchEditor::tool_button(Tool tool) const {
    const auto index = static_cast<std::size_t>(std::ranges::find(tools, tool) - tools.begin());
    return index < tool_buttons_.size() ? tool_buttons_[index].second : gui::Widget{};
}

gui::Point SketchEditor::to_window(Vec2 point) const {
    const auto rect = need(services_->ui.context.absolute_bounds(canvas_));
    return {rect.x + rect.width / 2 + static_cast<float>((point.x - center_.x) * scale_),
            rect.y + rect.height / 2 - static_cast<float>((point.y - center_.y) * scale_)};
}

Vec2 SketchEditor::to_sketch(gui::Point window) const {
    const auto rect = need(services_->ui.context.absolute_bounds(canvas_));
    return {center_.x + (window.x - rect.x - rect.width / 2) / scale_,
            center_.y - (window.y - rect.y - rect.height / 2) / scale_};
}

const model::Sketch* SketchEditor::active() const {
    return services_ ? services_->document.state().sketch(sketch_) : nullptr;
}

double SketchEditor::grid_step() const noexcept {
    // The smallest 1-2-5 step at least 12 pixels apart.
    const double base = std::pow(10.0, std::floor(std::log10(12 / scale_)));
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (base * m * scale_ >= 12) { return base * m; }
    }
    return base * 10;
}

SketchEditor::Snap SketchEditor::snap(gui::Point window) const {
    const auto at = to_sketch(window);
    if (const auto* sketch = active()) {
        if (const auto id = model::find_point(*sketch, at, snap_pixels / scale_)) {
            return {sketch->at(id), id};
        }
    }
    const double step = grid_step();
    return {{std::round(at.x / step) * step, std::round(at.y / step) * step}, 0};
}

void SketchEditor::handle(gui::Context& context, gui::RoutedEvent& event) {
    if (event.current != canvas_) { return; }
    if (const auto* pointer = event.input<gui::PointerEvent>()) {
        switch (pointer->action) {
        case gui::PointerAction::down:
            if (pointer->button == gui::PointerButton::primary) {
                static_cast<void>(context.focus(canvas_));
                click(snap(pointer->position));
            } else {
                pan_from_ = pointer->position;
                pan_center_ = center_;
                static_cast<void>(context.capture_pointer(canvas_, pointer->pointer));
            }
            event.prevent_default();
            break;
        case gui::PointerAction::move:
            if (pan_from_) {
                center_ = pan_center_ + Vec2{-(pointer->position.x - pan_from_->x) / scale_,
                                             (pointer->position.y - pan_from_->y) / scale_};
            }
            hover_ = snap(pointer->position);
            redraw();
            break;
        case gui::PointerAction::up:
            if (pan_from_ && pointer->button != gui::PointerButton::primary) {
                pan_from_.reset();
                static_cast<void>(context.release_pointer(pointer->pointer));
            }
            break;
        case gui::PointerAction::leave:
        case gui::PointerAction::cancel:
            hover_.reset();
            pan_from_.reset();
            redraw();
            break;
        }
    } else if (const auto* wheel = event.input<gui::WheelEvent>()) {
        // Zoom about the cursor: the sketch point under it stays put.
        const auto before = to_sketch(wheel->position);
        scale_ = std::clamp(scale_ * std::pow(wheel->precise ? 1.01 : 1.1, wheel->delta_y), 0.05, 400.0);
        center_ = center_ + (before - to_sketch(wheel->position));
        hover_ = snap(wheel->position);
        redraw();
        event.prevent_default();
    } else if (const auto* key = event.input<gui::KeyEvent>()) {
        if (!key->down || key->modifiers != gui::Modifiers::none) { return; }
        switch (key->key) {
        case gui::Key::escape:
            if (!pending_.empty()) {
                cancel();
            } else if (selection_) {
                selection_ = 0;
                redraw();
                notify();
            }
            break;
        case gui::Key::backspace:
        case gui::Key::delete_forward:
            if (selection_) {
                const auto removed = selection_;
                commit("Delete", [&](model::DocumentState&, model::Sketch& sketch) {
                    static_cast<void>(model::remove_entity(sketch, removed));
                });
            }
            break;
        case gui::Key::s: set_tool(Tool::select); break;
        case gui::Key::l: set_tool(Tool::line); break;
        case gui::Key::r: set_tool(Tool::rectangle); break;
        case gui::Key::c: set_tool(Tool::circle); break;
        case gui::Key::a: set_tool(Tool::arc); break;
        default: return;
        }
        event.prevent_default();
    }
}

void SketchEditor::click(const Snap& at) {
    const auto p = at.position;
    switch (tool_) {
    case Tool::select:
        pick(p);
        return;
    case Tool::line: {
        if (pending_.empty()) {
            pending_.push_back(p);
            break;
        }
        const auto from = pending_.back();
        if (from == p) { return; }
        commit("Line", [&](model::DocumentState& state, model::Sketch& sketch) {
            static_cast<void>(model::add_line(state, sketch, model::point_at(state, sketch, from, same_point),
                                              model::point_at(state, sketch, p, same_point)));
        });
        // Clicking the chain's first point closes it.
        if (p == pending_.front()) {
            pending_.clear();
        } else {
            pending_ = {pending_.front(), p};
        }
        break;
    }
    case Tool::rectangle:
        if (pending_.empty()) {
            pending_.push_back(p);
            break;
        }
        if (p.x != pending_[0].x && p.y != pending_[0].y) {
            const auto corner = pending_[0];
            commit("Rectangle", [&](model::DocumentState& state, model::Sketch& sketch) {
                static_cast<void>(model::add_rectangle(state, sketch, corner, p, same_point));
            });
            pending_.clear();
        }
        break;
    case Tool::circle:
        if (pending_.empty()) {
            pending_.push_back(p);
            break;
        }
        if (const double radius = model::distance(pending_[0], p); radius > 0) {
            const auto center = pending_[0];
            commit("Circle", [&](model::DocumentState& state, model::Sketch& sketch) {
                static_cast<void>(model::add_circle(state, sketch, model::point_at(state, sketch, center, same_point),
                                                    radius));
            });
            pending_.clear();
        }
        break;
    case Tool::arc:
        if (pending_.size() < 2) {
            if (pending_.empty() || pending_[0] != p) { pending_.push_back(p); }
            break;
        }
        if (p != pending_[0] && p != pending_[1]) {
            const auto center = pending_[0];
            const auto start = pending_[1];
            const double radius = model::distance(center, start);
            const auto direction = p - center;
            const auto end = center + direction * (radius / model::length(direction));
            commit("Arc", [&](model::DocumentState& state, model::Sketch& sketch) {
                static_cast<void>(model::add_arc(state, sketch, model::point_at(state, sketch, center, same_point),
                                                 model::point_at(state, sketch, start, same_point),
                                                 model::point_at(state, sketch, end, same_point)));
            });
            pending_.clear();
        }
        break;
    }
    redraw();
}

void SketchEditor::pick(Vec2 at) {
    const auto* sketch = active();
    model::ObjectId found = 0;
    if (sketch) {
        const double tolerance = snap_pixels / scale_;
        found = model::find_point(*sketch, at, tolerance);
        double best = tolerance;
        const auto consider = [&](model::ObjectId id, double d) {
            if (!found && d <= best) {
                best = d;
                found = id;
            }
        };
        for (const auto& l : sketch->lines) { consider(l.id, segment_distance(at, sketch->at(l.start), sketch->at(l.end))); }
        for (const auto& c : sketch->circles) {
            consider(c.id, std::abs(model::distance(at, sketch->at(c.center)) - c.radius));
        }
        for (const auto& a : sketch->arcs) {
            const auto center = sketch->at(a.center);
            const auto start = sketch->at(a.start);
            if (in_sweep(at, center, start, sketch->at(a.end))) {
                consider(a.id, std::abs(model::distance(at, center) - model::distance(center, start)));
            }
        }
    }
    selection_ = found;
    redraw();
    notify();
}

void SketchEditor::commit(std::string label,
                          const std::function<void(model::DocumentState&, model::Sketch&)>& draw) {
    const bool changed_state = services_->document.edit(label, [&](model::DocumentState& state) {
        auto* sketch = state.sketch(sketch_);
        if (!sketch) {
            sketch_ = model::add_sketch(state, model::SketchPlane::xy);
            sketch = state.sketch(sketch_);
        }
        draw(state, *sketch);
    });
    if (changed_state) {
        if (const auto* sketch = active()) { services_->report(label + " in " + sketch->name); }
    }
}

void SketchEditor::cancel() {
    pending_.clear();
    redraw();
}

void SketchEditor::redraw() {
    if (!services_ || !canvas_.valid()) { return; }
    need(services_->ui.context.invalidate(canvas_, gui::Dirty::shape));
    services_->ui.context.request_frame();
}

void SketchEditor::notify() {
    if (changed) { changed(); }
}

void SketchEditor::refresh_tools() {
    if (!services_) { return; }
    for (std::size_t i = 0; i < tool_buttons_.size(); ++i) {
        const auto fill = tools[i] == tool_ ? active_tool : gui::Color{};
        need(services_->ui.context.set_decoration(tool_buttons_[i].first, {.fill = fill, .corner_radius = 4}));
    }
}

void SketchEditor::paint(gui::ShapeWriter& out, gui::Size size) const {
    const auto local = [&](Vec2 v) {
        return gui::Point{size.width / 2 + static_cast<float>((v.x - center_.x) * scale_),
                          size.height / 2 - static_cast<float>((v.y - center_.y) * scale_)};
    };
    const auto polyline = [&](const std::vector<Vec2>& points, float width, gui::Color color) {
        for (std::size_t i = 1; i < points.size(); ++i) { out.line(local(points[i - 1]), local(points[i]), width, color); }
    };
    const auto dot = [&](Vec2 v, float radius, gui::Color color) {
        const auto p = local(v);
        out.rect({p.x - radius, p.y - radius, radius * 2, radius * 2}, {.fill = color, .corner_radius = radius});
    };

    // Grid, with every fifth line stronger and the axes coloured.
    const double step = grid_step();
    const double half_width = size.width / 2.0 / scale_;
    const double half_height = size.height / 2.0 / scale_;
    const auto grid = [&](bool vertical) {
        const double c = vertical ? center_.x : center_.y;
        const double half = vertical ? half_width : half_height;
        const double other = vertical ? center_.y : center_.x;
        const double other_half = vertical ? half_height : half_width;
        for (auto i = static_cast<long long>(std::floor((c - half) / step));
             i <= static_cast<long long>(std::ceil((c + half) / step)); ++i) {
            const double at = static_cast<double>(i) * step;
            gui::Color color{1, 1, 1, i % 5 == 0 ? 0.09F : 0.04F};
            if (i == 0) { color = vertical ? gui::Color{0.4F, 0.8F, 0.45F, 0.6F} : gui::Color{0.9F, 0.4F, 0.4F, 0.6F}; }
            const Vec2 a = vertical ? Vec2{at, other - other_half} : Vec2{other - other_half, at};
            const Vec2 b = vertical ? Vec2{at, other + other_half} : Vec2{other + other_half, at};
            out.line(local(a), local(b), 1, color);
        }
    };
    grid(true);
    grid(false);

    if (const auto* sketch = active()) {
        const auto color = [&](model::ObjectId id) { return id == selection_ ? accent : curve_color; };
        for (const auto& l : sketch->lines) {
            out.line(local(sketch->at(l.start)), local(sketch->at(l.end)), 2, color(l.id));
        }
        for (const auto& a : sketch->arcs) {
            polyline(arc_points(sketch->at(a.center), sketch->at(a.start), sketch->at(a.end)), 2, color(a.id));
        }
        for (const auto& c : sketch->circles) { polyline(circle_points(sketch->at(c.center), c.radius), 2, color(c.id)); }
        for (const auto& p : sketch->points) {
            dot(p.position, p.id == selection_ ? 4.5F : 3, p.id == selection_ ? accent : curve_color);
        }
    }

    // The shape being drawn, from the clicks so far to the cursor.
    if (hover_ && !pending_.empty()) {
        const auto h = hover_->position;
        switch (tool_) {
        case Tool::line: out.line(local(pending_.back()), local(h), 1.5F, preview); break;
        case Tool::rectangle: {
            const auto a = pending_[0];
            polyline({a, {h.x, a.y}, h, {a.x, h.y}, a}, 1.5F, preview);
            break;
        }
        case Tool::circle: polyline(circle_points(pending_[0], model::distance(pending_[0], h)), 1.5F, preview); break;
        case Tool::arc:
            if (pending_.size() == 1) {
                out.line(local(pending_[0]), local(h), 1, preview);
            } else if (h != pending_[0]) {
                polyline(arc_points(pending_[0], pending_[1], h), 1.5F, preview);
            }
            break;
        case Tool::select: break;
        }
    }
    for (const auto& p : pending_) { dot(p, 3, preview); }
    if (hover_ && tool_ != Tool::select) {
        const auto p = local(hover_->position);
        if (hover_->point) {
            out.rect({p.x - 6, p.y - 6, 12, 12}, {.border = accent, .border_width = 1.5F, .corner_radius = 6});
        } else {
            dot(hover_->position, 2, accent);
        }
    }
}

} // namespace studio::cad
