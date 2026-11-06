#include "studio/cad/sketch_editor.hpp"

#include "model/sketch_edit.hpp"
#include "studio/cad/sketch_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <variant>

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
constexpr gui::Color constrained_color{0.55F, 0.85F, 0.6F, 1};
constexpr gui::Color conflict_color{0.95F, 0.45F, 0.4F, 1};
constexpr float field_width = 64;
constexpr float field_height = 26;

std::string format_value(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3f", value);
    std::string text = buffer;
    while (text.back() == '0') { text.pop_back(); }
    if (text.back() == '.') { text.pop_back(); }
    return text;
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

const char* SketchEditor::action_name(Action action) noexcept {
    switch (action) {
    case Action::coincident: return "Coincident";
    case Action::horizontal: return "Horizontal";
    case Action::vertical: return "Vertical";
    case Action::parallel: return "Parallel";
    case Action::perpendicular: return "Perpendicular";
    case Action::equal: return "Equal";
    case Action::fixed: return "Fixed";
    case Action::dimension: return "Dimension";
    }
    return "?";
}

void SketchEditor::build(WorkspaceServices& services, gui::ElementId parent, gui::ElementId inspector) {
    services_ = &services;
    auto& context = services.ui.context;

    const auto bar = services.ui.row(parent, 4);
    for (const auto tool : tools) {
        const auto highlight = services.ui.row(bar, 0, {2, 2, 2, 2});
        tool_buttons_.emplace_back(highlight, services.ui.button(highlight, tool_name(tool), [this, tool] {
            set_tool(tool);
        }));
    }

    constraints_ = need(context.create(inspector));
    need(context.set_layout(constraints_, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 6})));
    need(context.set_clips_children(constraints_, true));
    static_cast<void>(services.ui.label(constraints_, "Constraints"));
    const auto grid = need(context.create(constraints_));
    constraint_grid_ = grid;
    constraint_columns_ = 2;
    need(context.set_layout(grid, std::make_shared<gui::GridLayout>(gui::GridOptions{
        .columns = {gui::Track::weighted(), gui::Track::weighted()},
        .column_spacing = 4, .row_spacing = 4})));
    for (const auto action : actions) {
        auto button = services.ui.button(grid, action_name(action), [this, action] {
            static_cast<void>(apply(action));
        });
        auto params = need(context.layout_params(button.element()));
        params.cell.column = static_cast<std::uint16_t>(action_buttons_.size() % 2);
        params.cell.row = static_cast<std::uint16_t>(action_buttons_.size() / 2);
        need(context.set_layout_params(button.element(), params));
        action_buttons_.push_back(button);
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
    need(context.set_clips_children(canvas_, true));
    refresh_tools();
    sync();
}

void SketchEditor::set_visible(bool visible) {
    visible_ = visible;
    if (!services_) { return; }
    const bool shown = visible && active() && !selection_.empty();
    gui::LayoutParams params;
    if (!shown) { params.height = 0; }
    need(services_->ui.context.set_layout_params(constraints_, params));
    need(services_->ui.context.set_interaction(constraints_, {.enabled = shown}));
    refresh_tools();
}

void SketchEditor::teardown() {
    if (services_ && dragging_) { services_->document.cancel(); }
    services_ = nullptr;
    canvas_ = {};
    constraints_ = {};
    constraint_grid_ = {};
    tool_buttons_.clear();
    action_buttons_.clear();
    fields_.clear();
    dragging_ = 0;
    pending_.clear();
    hover_.reset();
    pan_from_.reset();
}

void SketchEditor::sync() {
    if (!services_) { return; }
    const auto& state = services_->document.state();
    if (!state.sketch(sketch_)) { sketch_ = state.sketches.empty() ? 0 : state.sketches.back().id; }
    const auto* sketch = active();
    std::erase_if(selection_, [&](model::ObjectId id) { return !sketch || !sketch->contains(id); });
    status_ = sketch ? model::analyse(*sketch) : model::SolveResult{};
    redraw();
    notify();
}

void SketchEditor::set_tool(Tool tool) {
    tool_ = tool;
    cancel();
    refresh_tools();
    if (services_) {
        services_->report(std::string("Tool: ") + tool_name(tool));
        if (visible_) { need(services_->ui.context.focus(canvas_)); }
    }
    notify();
}

void SketchEditor::set_sketch(model::ObjectId sketch) {
    if (sketch == sketch_) { return; }
    sketch_ = sketch;
    selection_.clear();
    status_ = active() ? model::analyse(*active()) : model::SolveResult{};
    cancel();
    redraw();
    notify();
}

gui::Widget SketchEditor::tool_button(Tool tool) const {
    const auto index = static_cast<std::size_t>(std::ranges::find(tools, tool) - tools.begin());
    return index < tool_buttons_.size() ? tool_buttons_[index].second : gui::Widget{};
}

gui::Widget SketchEditor::action_button(Action action) const {
    const auto index = static_cast<std::size_t>(std::ranges::find(actions, action) - actions.begin());
    return index < action_buttons_.size() ? action_buttons_[index] : gui::Widget{};
}

gui::Widget SketchEditor::dimension_field(model::ObjectId constraint) const {
    const auto it = fields_.find(constraint);
    return it == fields_.end() ? gui::Widget{} : it->second.widget;
}

bool SketchEditor::editing_dimension(const gui::Context& context) const {
    return std::ranges::any_of(fields_, [&](const auto& entry) {
        return entry.second.widget.alive() && context.focused() == entry.second.widget.element();
    });
}

std::vector<SketchEditor::Request> SketchEditor::requests(Action action) const {
    const auto* sketch = active();
    if (!sketch || selection_.empty()) { return {}; }
    using Kind = model::ConstraintKind;
    std::vector<Request> requests;
    const auto& sel = selection_;
    const auto pair = [&](Kind kind) {
        if (sel.size() == 2) { requests.push_back({kind, sel[0], sel[1]}); }
    };
    switch (action) {
    case Action::horizontal:
    case Action::vertical:
    case Action::fixed: {
        const auto kind = action == Action::horizontal ? Kind::horizontal
                          : action == Action::vertical ? Kind::vertical
                                                       : Kind::fixed;
        for (const auto id : sel) { requests.push_back({kind, id, 0}); }
        break;
    }
    case Action::coincident:
        // The point goes first.
        if (sel.size() == 2) {
            const bool swap = !sketch->point(sel[0]);
            requests.push_back({Kind::coincident, sel[swap ? 1 : 0], sel[swap ? 0 : 1]});
        }
        break;
    case Action::parallel: pair(Kind::parallel); break;
    case Action::perpendicular: pair(Kind::perpendicular); break;
    case Action::equal: pair(Kind::equal); break;
    case Action::dimension:
        if (sel.size() == 1 && sketch->line(sel[0])) {
            requests.push_back({Kind::distance, sel[0], 0});
        } else if (sel.size() == 1 && sketch->circle(sel[0])) {
            requests.push_back({Kind::diameter, sel[0], 0});
        } else if (sel.size() == 1 && sketch->arc(sel[0])) {
            requests.push_back({Kind::radius, sel[0], 0});
        } else {
            pair(Kind::distance);
        }
        break;
    }
    return requests;
}

bool SketchEditor::can_apply(Action action) const {
    if (!visible_) { return false; }
    const auto pending = requests(action);
    return !pending.empty() && std::ranges::all_of(pending, [&](const Request& request) {
        return model::constraint_fits(*active(), request.kind, request.a, request.b);
    });
}

bool SketchEditor::apply(Action action) {
    if (!services_) { return false; }
    const std::string name = action_name(action);
    if (!active() || selection_.empty()) {
        services_->report(name + ": select sketch geometry first");
        return false;
    }
    const auto pending = requests(action);
    auto error = can_apply(action) ? model::ConstrainError::none : model::ConstrainError::unsuitable;
    if (error == model::ConstrainError::none) {
        need(services_->ui.context.focus(canvas_));
        static_cast<void>(services_->document.edit(name, [&](model::DocumentState& state) {
            const auto saved = state;
            auto* target = state.sketch(sketch_);
            for (const auto& r : pending) {
                if (error != model::ConstrainError::none) { break; }
                error = model::constrain(state, *target, r.kind, r.a, r.b).error;
            }
            if (error != model::ConstrainError::none) { state = saved; }
        }));
    }
    switch (error) {
    case model::ConstrainError::none: services_->report(name + " added to " + active()->name); return true;
    case model::ConstrainError::unsuitable: services_->report(name + " does not apply to the selection"); break;
    case model::ConstrainError::over_constrained: services_->report(name + " would over-constrain the sketch"); break;
    }
    return false;
}

std::optional<gui::Point> SketchEditor::dimension_anchor(const model::Sketch& sketch,
                                                         const model::SketchConstraint& c) const {
    const auto bounds = need(services_->ui.context.bounds(canvas_));
    const auto local = [&](Vec2 v) {
        return gui::Point{bounds.width / 2 + static_cast<float>((v.x - center_.x) * scale_),
                          bounds.height / 2 - static_cast<float>((v.y - center_.y) * scale_)};
    };
    if (c.kind == model::ConstraintKind::distance) {
        if (const auto* l = sketch.line(c.a)) {
            const auto a = local(sketch.at(l->start));
            const auto b = local(sketch.at(l->end));
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::max(std::sqrt(dx * dx + dy * dy), 1.0F);
            // Outside a counter-clockwise loop, clear of the line.
            const float nx = -dy / len;
            const float ny = dx / len;
            const float gap = 6 + std::abs(nx) * field_width / 2 + std::abs(ny) * field_height / 2;
            return gui::Point{(a.x + b.x) / 2 + nx * gap, (a.y + b.y) / 2 + ny * gap};
        }
        const auto a = local(sketch.at(c.a));
        const auto b = local(sketch.at(c.b));
        return gui::Point{(a.x + b.x) / 2, (a.y + b.y) / 2};
    }
    model::ObjectId center = 0;
    double radius = 0;
    if (const auto* circle = sketch.circle(c.a)) {
        center = circle->center;
        radius = circle->radius;
    } else if (const auto* arc = sketch.arc(c.a)) {
        center = arc->center;
        radius = model::distance(sketch.at(arc->center), sketch.at(arc->start));
    } else {
        return std::nullopt;
    }
    const auto edge = sketch.at(center) + Vec2{radius, radius} * std::numbers::sqrt2 * 0.5;
    const auto p = local(edge);
    return gui::Point{p.x + 24, p.y - 16};
}

void SketchEditor::before_frame() {
    if (!services_ || !constraints_.valid()) { return; }
    auto& context = services_->ui.context;
    const std::uint16_t columns = need(context.bounds(constraints_)).width >= 220 ? 2 : 1;
    if (columns == constraint_columns_) { return; }
    constraint_columns_ = columns;
    need(context.set_layout(constraint_grid_, std::make_shared<gui::GridLayout>(gui::GridOptions{
        .columns = std::vector<gui::Track>(columns, gui::Track::weighted()),
        .column_spacing = 4, .row_spacing = 4})));
    for (std::size_t i = 0; i < action_buttons_.size(); ++i) {
        auto params = need(context.layout_params(action_buttons_[i].element()));
        params.cell.column = static_cast<std::uint16_t>(i % columns);
        params.cell.row = static_cast<std::uint16_t>(i / columns);
        need(context.set_layout_params(action_buttons_[i].element(), params));
    }
}

void SketchEditor::update() {
    if (!services_ || !canvas_.valid()) { return; }
    auto& context = services_->ui.context;
    const auto* sketch = active();
    const auto wanted = [&](model::ObjectId id) {
        const auto* c = sketch ? sketch->constraint(id) : nullptr;
        return c && model::is_dimension(c->kind);
    };
    for (auto it = fields_.begin(); it != fields_.end();) {
        if (wanted(it->first)) {
            ++it;
        } else {
            if (it->second.widget.alive()) { need(context.destroy(it->second.widget.element())); }
            it = fields_.erase(it);
        }
    }
    if (!sketch) { return; }
    for (const auto& c : sketch->constraints) {
        if (!model::is_dimension(c.kind)) { continue; }
        auto it = fields_.find(c.id);
        if (it == fields_.end()) {
            const auto id = c.id;
            auto widget = services_->ui.widget(canvas_, {.kind = gui::WidgetKind::text_edit,
                                                          .text = format_value(c.value),
                                                          .activated = [this, id] { submit_dimension(id); }});
            it = fields_.emplace(id, Field{widget, {-1e9F, -1e9F}}).first;
        }
        auto& field = it->second;
        const auto anchor = dimension_anchor(*sketch, c).value_or(gui::Point{});
        const gui::Point position{std::round(anchor.x - field_width / 2), std::round(anchor.y - field_height / 2)};
        if (position.x != field.position.x || position.y != field.position.y) {
            gui::LayoutParams params;
            params.width = field_width;
            params.position = position;
            need(context.set_layout_params(field.widget.element(), params));
            field.position = position;
        }
        const auto text = format_value(c.value);
        if (context.focused() != field.widget.element() && need(field.widget.text()) != text) {
            need(field.widget.set_text(text));
        }
    }
}

void SketchEditor::submit_dimension(model::ObjectId constraint) {
    const auto it = fields_.find(constraint);
    if (!services_ || it == fields_.end()) { return; }
    const auto text = need(it->second.widget.text());
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !(value > 0)) {
        services_->report("Enter a positive number of millimetres");
    } else if (services_->document.edit("Edit dimension", [&](model::DocumentState& state) {
                   if (auto* sketch = state.sketch(sketch_)) {
                       static_cast<void>(model::set_dimension(*sketch, constraint, value));
                   }
               })) {
        services_->report("Dimension set to " + format_value(value) + " mm");
    } else if (const auto* sketch = active(); sketch && sketch->constraint(constraint) &&
                                              sketch->constraint(constraint)->value == value) {
        services_->report("Dimension unchanged");
    } else {
        services_->report("No solution with the dimension at " + format_value(value) + " mm");
    }
    // Hand focus back so the field shows the stored value again.
    static_cast<void>(services_->ui.context.focus(canvas_));
    services_->ui.context.request_frame();
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
    // Events bubbling up from the dimension fields belong to them.
    if (event.current != canvas_ || event.target != canvas_) { return; }
    if (const auto* pointer = event.input<gui::PointerEvent>()) {
        switch (pointer->action) {
        case gui::PointerAction::down:
            // One gesture at a time: a second button must not take over or
            // release the capture an ongoing drag or pan depends on.
            if (dragging_ || pan_from_) {
                event.prevent_default();
                break;
            }
            if (pointer->button == gui::PointerButton::primary) {
                static_cast<void>(context.focus(canvas_));
                if (tool_ != Tool::select) {
                    click(snap(pointer->position));
                    break;
                }
                const auto at = to_sketch(pointer->position);
                const bool extend = gui::has(pointer->modifiers, gui::Modifiers::shift);
                const auto* sketch = active();
                const auto found = hit(at);
                const bool fixed = sketch && std::ranges::any_of(sketch->constraints, [&](const auto& c) {
                    return c.kind == model::ConstraintKind::fixed && c.a == found;
                });
                if (!extend && sketch && sketch->point(found) && !fixed) {
                    // Drag the point; the solver keeps the constraints.
                    selection_ = {found};
                    dragging_ = found;
                    services_->document.begin("Move point");
                    static_cast<void>(context.capture_pointer(canvas_, pointer->pointer));
                    redraw();
                    notify();
                } else {
                    pick(at, extend);
                }
            } else {
                pan_from_ = pointer->position;
                pan_center_ = center_;
                static_cast<void>(context.capture_pointer(canvas_, pointer->pointer));
            }
            event.prevent_default();
            break;
        case gui::PointerAction::move:
            if (dragging_) { drag_to(to_sketch(pointer->position)); }
            if (pan_from_) {
                center_ = pan_center_ + Vec2{-(pointer->position.x - pan_from_->x) / scale_,
                                             (pointer->position.y - pan_from_->y) / scale_};
            }
            hover_ = snap(pointer->position);
            redraw();
            break;
        case gui::PointerAction::up:
            if (dragging_ && pointer->button == gui::PointerButton::primary) {
                dragging_ = 0;
                static_cast<void>(context.release_pointer(pointer->pointer));
                if (services_->document.commit()) { services_->report("Moved point"); }
            }
            if (pan_from_ && pointer->button != gui::PointerButton::primary) {
                pan_from_.reset();
                static_cast<void>(context.release_pointer(pointer->pointer));
            }
            break;
        case gui::PointerAction::leave:
        case gui::PointerAction::cancel:
            if (dragging_ && pointer->action == gui::PointerAction::cancel) {
                dragging_ = 0;
                services_->document.cancel();
            }
            hover_.reset();
            pan_from_.reset();
            redraw();
            break;
        }
    } else if (std::holds_alternative<gui::CaptureLostEvent>(event.payload)) {
        // Keep what was dragged so far; the transaction must not stay open.
        if (dragging_) {
            dragging_ = 0;
            if (services_->document.commit()) { services_->report("Moved point"); }
        }
        pan_from_.reset();
        redraw();
    } else if (const auto* wheel = event.input<gui::WheelEvent>()) {
        // Zoom about the cursor: the sketch point under it stays put.
        const auto before = to_sketch(wheel->position);
        scale_ = std::clamp(scale_ * std::pow(wheel->precise ? 1.01 : 1.1, wheel->delta_y), 0.05, 400.0);
        center_ = center_ + (before - to_sketch(wheel->position));
        hover_ = snap(wheel->position);
        redraw();
        event.prevent_default();
    } else if (const auto* key = event.input<gui::KeyEvent>()) {
        if (!key->down || key->modifiers != gui::Modifiers::none || dragging_) { return; }
        switch (key->key) {
        case gui::Key::escape:
            if (!pending_.empty()) {
                cancel();
            } else if (!selection_.empty()) {
                selection_.clear();
                redraw();
                notify();
            }
            break;
        case gui::Key::backspace:
        case gui::Key::delete_forward:
            if (!selection_.empty()) {
                const auto removed = selection_;
                commit("Delete", [&](model::DocumentState&, model::Sketch& sketch) {
                    for (const auto id : removed) { static_cast<void>(model::remove_entity(sketch, id)); }
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
    case Tool::select: return;
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

void SketchEditor::drag_to(Vec2 at) {
    const auto point = dragging_;
    services_->document.update([&](model::DocumentState& state) {
        auto* sketch = state.sketch(sketch_);
        if (!sketch) { return; }
        auto moved = *sketch;
        for (auto& p : moved.points) {
            if (p.id == point) { p.position = at; }
        }
        const model::ObjectId pinned[] = {point};
        // Positions the constraints cannot reach are skipped.
        if (model::solve(moved, pinned).converged) { *sketch = std::move(moved); }
    });
}

void SketchEditor::pick(Vec2 at, bool extend) {
    const auto found = hit(at);
    if (!extend) {
        selection_.clear();
        if (found) { selection_.push_back(found); }
    } else if (found) {
        if (const auto it = std::ranges::find(selection_, found); it != selection_.end()) {
            selection_.erase(it);
        } else {
            selection_.push_back(found);
        }
    }
    redraw();
    notify();
}

model::ObjectId SketchEditor::hit(Vec2 at) const {
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
    return found;
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
    if (services_) { set_visible(visible_); }
    if (changed) { changed(); }
}

void SketchEditor::refresh_tools() {
    if (!services_) { return; }
    for (std::size_t i = 0; i < tool_buttons_.size(); ++i) {
        services_->ui.highlight(tool_buttons_[i].first, tools[i] == tool_);
    }
    for (std::size_t i = 0; i < action_buttons_.size(); ++i) {
        need(action_buttons_[i].set_enabled(can_apply(actions[i])));
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
        const auto selected = [&](model::ObjectId id) { return std::ranges::find(selection_, id) != selection_.end(); };
        const auto base = status_.status == model::SolveStatus::over   ? conflict_color
                          : status_.status == model::SolveStatus::full ? constrained_color
                                                                       : curve_color;
        const auto color = [&](model::ObjectId id) { return selected(id) ? accent : base; };
        for (const auto& l : sketch->lines) {
            out.line(local(sketch->at(l.start)), local(sketch->at(l.end)), 2, color(l.id));
        }
        for (const auto& a : sketch->arcs) {
            polyline(arc_points(sketch->at(a.center), sketch->at(a.start), sketch->at(a.end)), 2, color(a.id));
        }
        for (const auto& c : sketch->circles) { polyline(circle_points(sketch->at(c.center), c.radius), 2, color(c.id)); }
        for (const auto& p : sketch->points) { dot(p.position, selected(p.id) ? 4.5F : 3, color(p.id)); }
        for (const auto& c : sketch->constraints) {
            if (c.kind == model::ConstraintKind::fixed) {
                const auto p = local(sketch->at(c.a));
                out.rect({p.x - 6, p.y - 6, 12, 12}, {.border = base, .border_width = 1.5F});
            }
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
