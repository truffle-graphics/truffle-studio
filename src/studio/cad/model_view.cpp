#include "studio/cad/model_view.hpp"

#include "studio/cad/sketch_geometry.hpp"
#include "studio/scene_view.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <variant>

namespace studio::cad {
namespace gui = truffle::gui;

namespace {
constexpr gui::Color background{0.11F, 0.12F, 0.14F, 1};
constexpr gui::Color sketch_color{0.86F, 0.88F, 0.92F, 0.85F};
constexpr gui::Color active_color{0.35F, 0.6F, 1, 1};

Vec3 to_vec(kernel::Vec3 v) { return {v.x, v.y, v.z}; }
} // namespace

void ModelView::build(WorkspaceServices& services, gui::ElementId parent) {
    services_ = &services;
    auto& context = services.ui.context;
    view_ = need(context.create(parent));
    gui::LayoutParams params;
    params.weight = 1;
    need(context.set_layout_params(view_, params));
    need(context.set_decoration(view_, {.fill = background}));
    need(context.set_shape_builder(view_, std::make_shared<gui::CanvasBuilder>(
        [this](gui::ShapeWriter& out, gui::Size size) { paint(out, size); })));
    need(context.set_interaction(view_, {.hit_test = true, .enabled = true, .focusable = true}));
    need(context.set_event_handler(view_, [this](gui::Context& ctx, gui::RoutedEvent& event) {
        handle(ctx, event);
    }));
    need(context.set_clips_children(view_, true));
}

void ModelView::teardown() {
    services_ = nullptr;
    view_ = {};
    shown_ = {};
    visible_ = false;
    drag_ = Drag::none;
}

void ModelView::set_mesh(const kernel::Mesh& mesh) {
    if (services_ && services_->scene) { services_->scene->set_mesh(mesh); }
    mesh_bounds_.reset();
    for (std::size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
        const kernel::Vec3 p{mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]};
        if (!mesh_bounds_) {
            mesh_bounds_ = kernel::Bounds{p, p};
            continue;
        }
        auto& b = *mesh_bounds_;
        b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
        b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
    }
    redraw();
}

void ModelView::fit() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    Vec3 low{inf, inf, inf};
    Vec3 high{-inf, -inf, -inf};
    const auto include = [&](Vec3 p) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    };
    if (mesh_bounds_) {
        include(to_vec(mesh_bounds_->min));
        include(to_vec(mesh_bounds_->max));
    }
    if (services_) {
        for (const auto& sketch : services_->document.state().sketches) {
            for (const auto& point : sketch.points) { include(on_plane(sketch.plane, point.position)); }
        }
    }
    if (low.x > high.x) {
        low = {-50, -50, -50};
        high = {50, 50, 50};
    }
    camera.fit(low, high);
    redraw();
}

void ModelView::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) { drag_ = Drag::none; }
    redraw();
}

void ModelView::set_active_sketch(model::ObjectId sketch) {
    if (active_ == sketch) { return; }
    active_ = sketch;
    redraw();
}

void ModelView::redraw() {
    if (!services_ || !view_.valid()) { return; }
    need(services_->ui.context.invalidate(view_, gui::Dirty::shape));
    services_->ui.context.request_frame();
}

void ModelView::before_frame() {
    if (!services_ || !services_->scene || !visible_) { return; }
    auto& context = services_->ui.context;
    const auto rect = need(context.absolute_bounds(view_));
    const float scale = context.scale();
    const auto width = static_cast<std::uint32_t>(std::lround(rect.width * scale));
    const auto height = static_cast<std::uint32_t>(std::lround(rect.height * scale));
    if (width == 0 || height == 0) { return; }

    const auto light = normalize(camera.forward() * -1 + camera.up() * 0.5 + camera.right() * 0.3);
    SceneFrame frame{.extent = {width, height},
                     .view_projection = camera.view_projection(static_cast<double>(width) / height),
                     .light = {static_cast<float>(light.x), static_cast<float>(light.y), static_cast<float>(light.z)},
                     .clear = {background.r, background.g, background.b, background.a}};
    const auto texture = services_->scene->request(frame);
    if (texture != shown_) {
        shown_ = texture;
        need(context.set_decoration(view_, {.fill = {1, 1, 1, 1}, .texture = texture}));
    }
}

void ModelView::paint(gui::ShapeWriter& out, gui::Size size) const {
    if (!services_) { return; }
    const auto segment = [&](Vec3 a, Vec3 b, float width, gui::Color color) {
        const auto pa = camera.project(a, size.width, size.height);
        const auto pb = camera.project(b, size.width, size.height);
        if (!pa || !pb) { return; }
        out.line({static_cast<float>((*pa)[0]), static_cast<float>((*pa)[1])},
                 {static_cast<float>((*pb)[0]), static_cast<float>((*pb)[1])}, width, color);
    };

    // Axes from the origin, sized to the view.
    const double axis = camera.distance * 0.15;
    segment({0, 0, 0}, {axis, 0, 0}, 1.5F, {0.9F, 0.4F, 0.4F, 0.8F});
    segment({0, 0, 0}, {0, axis, 0}, 1.5F, {0.4F, 0.8F, 0.45F, 0.8F});
    segment({0, 0, 0}, {0, 0, axis}, 1.5F, {0.4F, 0.55F, 0.95F, 0.8F});

    for (const auto& sketch : services_->document.state().sketches) {
        const auto color = sketch.id == active_ ? active_color : sketch_color;
        for (const auto& line : sketch_polylines(sketch)) {
            for (std::size_t i = 1; i < line.size(); ++i) {
                segment(on_plane(sketch.plane, line[i - 1]), on_plane(sketch.plane, line[i]), 1.5F, color);
            }
        }
    }
}

void ModelView::end_drag(gui::Context& context) {
    if (drag_ == Drag::none) { return; }
    drag_ = Drag::none;
    static_cast<void>(context.release_pointer(pointer_));
}

void ModelView::handle(gui::Context& context, gui::RoutedEvent& event) {
    if (event.current != view_ || event.target != view_) { return; }
    if (const auto* pointer = event.input<gui::PointerEvent>()) {
        switch (pointer->action) {
        case gui::PointerAction::down:
            if (drag_ != Drag::none) { break; }
            static_cast<void>(context.focus(view_));
            drag_ = pointer->button == gui::PointerButton::primary ? Drag::orbit : Drag::pan;
            pointer_ = pointer->pointer;
            last_ = pointer->position;
            static_cast<void>(context.capture_pointer(view_, pointer->pointer));
            event.prevent_default();
            break;
        case gui::PointerAction::move:
            if (drag_ != Drag::none) {
                const double dx = pointer->position.x - last_.x;
                const double dy = pointer->position.y - last_.y;
                last_ = pointer->position;
                if (drag_ == Drag::orbit) {
                    camera.orbit(dx, dy);
                } else {
                    camera.pan(dx, dy, need(context.absolute_bounds(view_)).height);
                }
                redraw();
            }
            break;
        case gui::PointerAction::up: end_drag(context); break;
        case gui::PointerAction::cancel: end_drag(context); break;
        default: break;
        }
    } else if (const auto* wheel = event.input<gui::WheelEvent>()) {
        const double step = wheel->precise ? wheel->delta_y * 0.01 : wheel->delta_y * 0.15;
        camera.zoom(std::exp(-step));
        redraw();
        event.prevent_default();
    } else if (std::holds_alternative<gui::CaptureLostEvent>(event.payload)) {
        drag_ = Drag::none;
    }
}

} // namespace studio::cad
