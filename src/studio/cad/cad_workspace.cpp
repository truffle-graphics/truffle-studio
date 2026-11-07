#include "studio/cad/cad_workspace.hpp"

#include "model/sketch_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace studio::cad {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

namespace {
// A vertical stack under `parent` that takes the leftover height.
gui::ElementId page(gui::Context& context, gui::ElementId parent) {
    const auto element = need(context.create(parent));
    need(context.set_layout(element, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 8})));
    need(context.set_clips_children(element, true));
    return element;
}

std::string plural(std::size_t count, const char* noun) {
    return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
}

std::string millimetres(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", value);
    return text;
}
} // namespace

EngineProfile CadWorkspace::profile() const {
    // Precise, on-demand rendering: frames only follow edits and navigation.
    return {.name = "CAD", .clear = {0.11F, 0.12F, 0.14F, 1}, .depth = true, .continuous = false};
}

void CadWorkspace::build(WorkspaceServices& services, dock::DockSpace& space) {
    services_ = &services;
    const auto& ui = services.ui;

    const auto model = ui.column(8);
    static_cast<void>(ui.label(model, "New sketch"));
    const auto planes = ui.row(model, 4);
    for (const auto plane : {model::SketchPlane::xy, model::SketchPlane::xz, model::SketchPlane::yz}) {
        auto button = ui.button(planes, model::plane_name(plane), [this, plane] {
            model::ObjectId created = 0;
            if (services_->document.edit("New sketch", [&](model::DocumentState& state) {
                    created = model::add_sketch(state, plane);
                })) {
                selected_extrude_ = 0;
                editor.set_sketch(created);
                show_model(false);
            }
        });
        auto params = need(ui.context.layout_params(button.element()));
        params.weight = 1;
        need(ui.context.set_layout_params(button.element(), params));
        plane_buttons.push_back(button);
        if (plane == model::SketchPlane::xy) { new_sketch = button; }
    }
    static_cast<void>(ui.label(model, "Features"));
    const auto features = ui.widget(model, {.kind = gui::WidgetKind::scroll_area});
    gui::LayoutParams feature_params;
    feature_params.weight = 1;
    need(ui.context.set_layout_params(features.element(), feature_params));
    feature_list_ = features.content();
    need(ui.context.set_layout(feature_list_, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 2})));
    extrude_button = ui.button(model, "Extrude sketch", [this] { extrude_active_sketch(); });

    const auto viewport = ui.column(6, 4);
    const auto modes = ui.row(viewport, 4);
    sketch_highlight_ = ui.row(modes, 0, {2, 2, 2, 2});
    sketch_mode = ui.button(sketch_highlight_, "Sketch", [this] { show_model(false); });
    model_highlight_ = ui.row(modes, 0, {2, 2, 2, 2});
    model_mode = ui.button(model_highlight_, "3D", [this] { show_model(true); });
    fit_view = ui.button(modes, "Fit", [this] {
        view.fit();
    });
    sketch_page_ = page(ui.context, viewport);
    model_page_ = page(ui.context, viewport);
    const auto inspector_panel = ui.column(0, 0);
    const auto inspector_scroll = ui.widget(inspector_panel, {.kind = gui::WidgetKind::scroll_area});
    gui::LayoutParams inspector_params;
    inspector_params.weight = 1;
    need(ui.context.set_layout_params(inspector_scroll.element(), inspector_params));
    const auto inspector = inspector_scroll.content();
    need(ui.context.set_layout(inspector, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 8})));
    inspector_ = inspector;
    static_cast<void>(ui.label(inspector, "Part name"));
    part_name = ui.widget(inspector, {
        .kind = gui::WidgetKind::text_edit, .text = services.document.state().name,
        .text_changed = [this](const std::string& value) {
            if (services_->document.edit("Rename part",
                                         [&](model::DocumentState& state) { state.name = value; })) {
                services_->report("Part name: " + value);
            }
        }});
    sketch_info = ui.label(inspector, "");
    editor.changed = [this] { refresh(); };
    editor.build(services, sketch_page_, inspector);
    view.build(services, model_page_);

    need(space.add_panel("model", "Model", model));
    need(space.add_panel("viewport", "Viewport", viewport));
    need(space.add_panel("inspector", "Inspector", inspector_panel));
    need(space.set_layout(dock::DockNode::split(dock::Axis::horizontal, {
        dock::DockNode::tabs({"model"}),
        dock::DockNode::tabs({"viewport"}),
        dock::DockNode::tabs({"inspector"})}, {0.18F, 0.64F, 0.18F})));
    subscription_ = services.document.subscribe([this] { sync(); });
    show_model(false);
    regenerate();
    rebuild_feature_list();
    refresh();
}

void CadWorkspace::teardown() {
    if (services_) { services_->document.unsubscribe(subscription_); }
    editor.teardown();
    editor.changed = {};
    view.teardown();
    sketch_mode = {};
    model_mode = {};
    fit_view = {};
    sketch_page_ = {};
    model_page_ = {};
    sketch_highlight_ = {};
    model_highlight_ = {};
    part_name = {};
    sketch_info = {};
    new_sketch = {};
    plane_buttons.clear();
    extrude_button = {};
    extrude_distance = {};
    sketch_buttons.clear();
    extrude_buttons.clear();
    entries_.clear();
    listed_.clear();
    feature_list_ = {};
    inspector_ = {};
    distance_row_ = {};
    distance_for_ = 0;
    selected_extrude_ = 0;
    fit_pending_ = false;
    built_extrudes_.clear();
    built_sketches_.clear();
    built_ = false;
    regeneration_ = {};
    reported_errors_.clear();
    services_ = nullptr;
}

void CadWorkspace::update() {
    if (!services_) { return; }
    if (selected_extrude_ != 0 && !services_->document.state().extrude(selected_extrude_)) { selected_extrude_ = 0; }
    regenerate();
    // The list and the distance field are rebuilt here, not in sync(): a
    // widget that changes the document must not be destroyed while its
    // event is dispatched.
    if (feature_signature() != listed_) { rebuild_feature_list(); }
    update_distance_field();
    editor.update();
}

void CadWorkspace::regenerate() {
    const auto& state = services_->document.state();
    std::vector<model::Sketch> used;
    for (const auto& extrude : state.extrudes) {
        if (const auto* sketch = state.sketch(extrude.sketch);
            sketch && std::ranges::none_of(used, [&](const auto& s) { return s.id == sketch->id; })) {
            used.push_back(*sketch);
        }
    }
    if (built_ && used == built_sketches_ && state.extrudes == built_extrudes_) { return; }
    built_ = true;
    built_sketches_ = std::move(used);
    built_extrudes_ = state.extrudes;
    regeneration_ = features::regenerate(state);
    view.set_mesh(regeneration_.mesh);
    if (fit_pending_) {
        view.fit();
        fit_pending_ = false;
    }
    std::string errors;
    for (const auto& result : regeneration_.features) {
        if (result.error.empty()) { continue; }
        const auto* extrude = state.extrude(result.feature);
        errors += (extrude ? extrude->name : std::string("Feature")) + ": " + result.error + "\n";
    }
    if (errors != reported_errors_) {
        reported_errors_ = errors;
        if (!errors.empty()) { services_->report(errors.substr(0, errors.find('\n'))); }
    }
    refresh();
}

void CadWorkspace::extrude_active_sketch() {
    const auto sketch = editor.sketch();
    if (!services_->document.state().sketch(sketch)) {
        services_->report("Select a sketch to extrude");
        return;
    }
    model::ObjectId created = 0;
    if (!services_->document.edit("Extrude",
                                  [&](model::DocumentState& state) { created = model::add_extrude(state, sketch, 10); })) {
        return;
    }
    selected_extrude_ = created;
    fit_pending_ = true;
    show_model(true);
    services_->report(services_->document.state().extrude(created)->name + " created");
}

void CadWorkspace::update_distance_field() {
    auto& context = services_->ui.context;
    const auto* extrude = services_->document.state().extrude(selected_extrude_);
    if (distance_for_ != selected_extrude_ || !extrude) {
        if (distance_row_.valid()) { need(context.destroy(distance_row_)); }
        distance_row_ = {};
        extrude_distance = {};
        distance_for_ = 0;
    }
    if (!extrude) { return; }
    const auto text = millimetres(extrude->distance);
    if (!distance_row_.valid()) {
        distance_row_ = need(context.create(inspector_));
        need(context.set_layout(distance_row_, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 4})));
        static_cast<void>(services_->ui.label(distance_row_, "Distance (mm)"));
        extrude_distance = services_->ui.widget(distance_row_, {.kind = gui::WidgetKind::text_edit, .text = text,
                                                                .activated = [this] { submit_distance(); }});
        distance_for_ = selected_extrude_;
    } else if (context.focused() != extrude_distance.element() && need(extrude_distance.text()) != text) {
        need(extrude_distance.set_text(text));
    }
}

void CadWorkspace::submit_distance() {
    if (!extrude_distance.alive()) { return; }
    const auto id = selected_extrude_;
    const auto text = need(extrude_distance.text());
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') { value = 0; }
    if (!std::isfinite(value) || value == 0) {
        services_->report("Enter a non-zero distance in millimetres");
    } else if (services_->document.edit("Edit extrude distance", [&](model::DocumentState& state) {
                   if (auto* extrude = state.extrude(id)) { extrude->distance = value; }
               })) {
        services_->report("Distance set to " + millimetres(value) + " mm");
    } else {
        services_->report("Distance unchanged");
    }
    if (std::isfinite(value) && value != 0) { static_cast<void>(services_->ui.context.focus(view.element())); }
}

void CadWorkspace::before_frame() {
    editor.before_frame();
    view.before_frame();
}

void CadWorkspace::show_model(bool model) {
    if (!services_) { return; }
    auto& context = services_->ui.context;
    const auto show = [&](gui::ElementId element, bool shown) {
        gui::LayoutParams params;
        params.weight = shown ? 1.0F : 0.0F;
        if (!shown) { params.height = 0.0F; }
        need(context.set_layout_params(element, params));
        need(context.set_interaction(element, {.hit_test = false, .enabled = shown}));
    };
    show(sketch_page_, !model);
    show(model_page_, model);
    services_->ui.highlight(sketch_highlight_, !model);
    services_->ui.highlight(model_highlight_, model);
    editor.set_visible(!model);
    view.set_visible(model);
    need(fit_view.set_enabled(model));
    if (!model) { selected_extrude_ = 0; }
    refresh();
    if (model) { static_cast<void>(context.focus(view.element())); }
    context.request_frame();
}

bool CadWorkspace::text_editing(const gui::Context& context) const {
    const auto focused = [&](const gui::Widget& widget) {
        return widget.alive() && context.focused() == widget.element();
    };
    return focused(part_name) || focused(extrude_distance) || editor.editing_dimension(context);
}

void CadWorkspace::sync() {
    const auto& value = services_->document.state().name;
    if (part_name.alive() && need(part_name.text()) != value) { need(part_name.set_text(value)); }
    editor.sync();
    view.redraw();
    services_->ui.context.request_frame();
}

std::string CadWorkspace::feature_signature() const {
    const auto& state = services_->document.state();
    std::string signature;
    for (const auto& sketch : state.sketches) {
        signature += 's' + std::to_string(sketch.id) + ':' + sketch.name + ':' + model::plane_name(sketch.plane) + ';';
    }
    for (const auto& extrude : state.extrudes) {
        const auto* result = regeneration_.result(extrude.id);
        signature += 'e' + std::to_string(extrude.id) + ':' + extrude.name + ':' + std::to_string(extrude.sketch) +
                     (result && !result->error.empty() ? "!" : "") + ';';
    }
    return signature;
}

void CadWorkspace::rebuild_feature_list() {
    auto& context = services_->ui.context;
    const auto& state = services_->document.state();
    for (const auto& entry : entries_) { need(context.destroy(entry.highlight)); }
    entries_.clear();
    sketch_buttons.clear();
    extrude_buttons.clear();
    // Sketches and extrudes share one id sequence, so id order is creation order.
    std::vector<std::pair<model::ObjectId, bool>> order;
    for (const auto& sketch : state.sketches) { order.emplace_back(sketch.id, false); }
    for (const auto& extrude : state.extrudes) { order.emplace_back(extrude.id, true); }
    std::ranges::sort(order);
    for (const auto& [id, is_extrude] : order) {
        const auto highlight = services_->ui.row(feature_list_, 0, {2, 2, 2, 2});
        if (is_extrude) {
            const auto& extrude = *state.extrude(id);
            const auto* result = regeneration_.result(id);
            auto label = extrude.name;
            if (result && !result->error.empty()) { label += " (failed)"; }
            extrude_buttons.push_back(services_->ui.button(highlight, label, [this, id] {
                selected_extrude_ = id;
                show_model(true);
                refresh();
            }));
            entries_.push_back({highlight, 0, id});
        } else {
            const auto& sketch = *state.sketch(id);
            sketch_buttons.push_back(services_->ui.button(
                highlight, sketch.name + " (" + model::plane_name(sketch.plane) + ")", [this, id] {
                    selected_extrude_ = 0;
                    editor.set_sketch(id);
                    show_model(false);
                    refresh();
                }));
            entries_.push_back({highlight, id, 0});
        }
    }
    listed_ = feature_signature();
    refresh();
}

void CadWorkspace::refresh() {
    if (!services_) { return; }
    auto& context = services_->ui.context;
    view.set_active_sketch(editor.sketch());
    for (const auto& entry : entries_) {
        const bool chosen = selected_extrude_ != 0 ? entry.extrude == selected_extrude_
                                                   : entry.extrude == 0 && entry.sketch == editor.sketch();
        services_->ui.highlight(entry.highlight, chosen);
    }
    const auto* active_sketch = services_->document.state().sketch(editor.sketch());
    need(extrude_button.set_enabled(active_sketch && !model::find_loops(*active_sketch).closed.empty()));
    if (!sketch_info.alive()) { return; }
    const auto& state = services_->document.state();
    std::string info;
    if (const auto* extrude = state.extrude(selected_extrude_)) {
        const auto* sketch = state.sketch(extrude->sketch);
        info = extrude->name + "\nSketch: " + (sketch ? sketch->name : std::string("missing")) +
               "\nPlane: " + (sketch ? model::plane_name(sketch->plane) : "unknown") + "\n";
        const auto* result = regeneration_.result(extrude->id);
        if (result && !result->error.empty()) {
            info += "Failed: " + result->error;
        } else if (result) {
            info += "Volume " + millimetres(std::round(studio::kernel::volume(result->solid) * 100) / 100) + " mm3";
        }
    } else if (const auto* sketch = state.sketch(editor.sketch())) {
        const auto loops = model::find_loops(*sketch);
        info = sketch->name + " on " + model::plane_name(sketch->plane) + "\n" +
               plural(sketch->curve_count(), "curve") + "\n" + plural(sketch->points.size(), "point") + "\n" +
               plural(loops.closed.size(), "closed profile");
        if (loops.open) { info += "\n" + plural(loops.open, "open chain"); }
        const auto& status = editor.status();
        info += "\n" + plural(sketch->constraints.size(), "constraint") + "\n";
        switch (status.status) {
        case model::SolveStatus::full: info += "Fully constrained"; break;
        case model::SolveStatus::under: info += "Under-constrained\n" + std::to_string(status.freedom) + " DOF"; break;
        case model::SolveStatus::over: info += "Over-constrained"; break;
        }
        if (!editor.selection().empty()) { info += "\n" + plural(editor.selection().size(), "item") + " selected"; }
    } else {
        info = "No feature selected.\n\nCreate a sketch on\nXY, XZ or YZ, or draw\nto start on XY.";
    }
    if (need(sketch_info.text()) != info) { need(sketch_info.set_text(info)); }
    context.request_frame();
}

} // namespace studio::cad
