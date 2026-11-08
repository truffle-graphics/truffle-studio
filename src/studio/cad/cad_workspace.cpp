#include "studio/cad/cad_workspace.hpp"

#include "model/sketch_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace studio::cad {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

namespace {
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
    auto& ui = services.ui;

    auto model = space.panel(ui, "model", "Model");
    static_cast<void>(model.label("New sketch"));
    auto planes = model.row();
    for (const auto plane : {model::SketchPlane::xy, model::SketchPlane::xz, model::SketchPlane::yz}) {
        auto button = planes.button(model::plane_name(plane)).weight(1).on_click([this, plane] {
            model::ObjectId created = 0;
            if (services_->document.edit("New sketch", [&](model::DocumentState& state) {
                    created = model::add_sketch(state, plane);
                })) {
                selected_extrude_ = 0;
                editor.set_sketch(created);
                show_model(false);
            }
        });
        plane_buttons.push_back(button);
        if (plane == model::SketchPlane::xy) { new_sketch = button; }
    }
    static_cast<void>(model.label("Features"));
    feature_list_ = model.scroll().weight(1);
    extrude_button = model.button("Extrude sketch").on_click([this] { extrude_active_sketch(); });

    auto viewport = space.panel(ui, "viewport", "Viewport");
    auto modes = viewport.row();
    // The selection rings around the mode buttons have no Truffle default.
    sketch_highlight_ = modes.row().padding(2);
    sketch_mode = sketch_highlight_.button("Sketch").on_click([this] { show_model(false); });
    model_highlight_ = modes.row().padding(2);
    model_mode = model_highlight_.button("3D").on_click([this] { show_model(true); });
    fit_view = modes.button("Fit").on_click([this] { view.fit(); });
    sketch_page_ = viewport.column().weight(1);
    model_page_ = viewport.column().weight(1);
    need(ui.context().set_clips_children(sketch_page_.element(), true));
    need(ui.context().set_clips_children(model_page_.element(), true));

    auto inspector_panel = space.panel(ui, "inspector", "Inspector");
    inspector_ = inspector_panel.scroll().weight(1);
    static_cast<void>(inspector_.label("Part name"));
    part_name = inspector_.text_field(services.document.state().name)
                    .on_change([this](const std::string& value) {
                        if (services_->document.edit("Rename part",
                                                     [&](model::DocumentState& state) { state.name = value; })) {
                            services_->report("Part name: " + value);
                        }
                    });
    sketch_info = inspector_.label("");
    editor.changed = [this] { refresh(); };
    editor.build(services, sketch_page_, inspector_);
    view.build(services, model_page_.element());

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
    auto& context = services_->ui.context();
    const auto* extrude = services_->document.state().extrude(selected_extrude_);
    if (distance_for_ != selected_extrude_ || !extrude) {
        distance_row_.remove();
        distance_row_ = {};
        extrude_distance = {};
        distance_for_ = 0;
    }
    if (!extrude) { return; }
    const auto text = millimetres(extrude->distance);
    if (!distance_row_.valid()) {
        distance_row_ = inspector_.column();
        static_cast<void>(distance_row_.label("Distance (mm)"));
        extrude_distance = distance_row_.text_field(text).on_submit([this] { submit_distance(); });
        distance_for_ = selected_extrude_;
    } else if (context.focused() != extrude_distance.element() && extrude_distance.text() != text) {
        extrude_distance.text(text);
    }
}

void CadWorkspace::submit_distance() {
    if (!extrude_distance.valid()) { return; }
    const auto id = selected_extrude_;
    const auto text = extrude_distance.text();
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
    if (std::isfinite(value) && value != 0) { static_cast<void>(services_->ui.context().focus(view.element())); }
}

void CadWorkspace::before_frame() {
    editor.before_frame();
    view.before_frame();
}

void CadWorkspace::show_model(bool model) {
    if (!services_) { return; }
    auto& context = services_->ui.context();
    sketch_page_.visible(!model);
    model_page_.visible(model);
    highlight(context, sketch_highlight_.element(), !model);
    highlight(context, model_highlight_.element(), model);
    editor.set_visible(!model);
    view.set_visible(model);
    fit_view.enabled(model);
    if (!model) { selected_extrude_ = 0; }
    refresh();
    if (model) { static_cast<void>(context.focus(view.element())); }
    context.request_frame();
}

bool CadWorkspace::text_editing(const gui::Context& context) const {
    const auto focused = [&](const gui::TextField& field) {
        return field.valid() && context.focused() == field.element();
    };
    return focused(part_name) || focused(extrude_distance) || editor.editing_dimension(context);
}

void CadWorkspace::sync() {
    const auto& value = services_->document.state().name;
    if (part_name.valid() && part_name.text() != value) { part_name.text(value); }
    editor.sync();
    view.redraw();
    services_->ui.context().request_frame();
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
    const auto& state = services_->document.state();
    for (auto& entry : entries_) { entry.highlight.remove(); }
    entries_.clear();
    sketch_buttons.clear();
    extrude_buttons.clear();
    // Sketches and extrudes share one id sequence, so id order is creation order.
    std::vector<std::pair<model::ObjectId, bool>> order;
    for (const auto& sketch : state.sketches) { order.emplace_back(sketch.id, false); }
    for (const auto& extrude : state.extrudes) { order.emplace_back(extrude.id, true); }
    std::ranges::sort(order);
    for (const auto& [id, is_extrude] : order) {
        auto highlight = feature_list_.row().padding(2);
        if (is_extrude) {
            const auto& extrude = *state.extrude(id);
            const auto* result = regeneration_.result(id);
            auto label = extrude.name;
            if (result && !result->error.empty()) { label += " (failed)"; }
            extrude_buttons.push_back(highlight.button(label).on_click([this, id] {
                selected_extrude_ = id;
                show_model(true);
                refresh();
            }));
            entries_.push_back({highlight, 0, id});
        } else {
            const auto& sketch = *state.sketch(id);
            sketch_buttons.push_back(
                highlight.button(sketch.name + " (" + model::plane_name(sketch.plane) + ")")
                    .on_click([this, id] {
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
    auto& context = services_->ui.context();
    view.set_active_sketch(editor.sketch());
    for (const auto& entry : entries_) {
        const bool chosen = selected_extrude_ != 0 ? entry.extrude == selected_extrude_
                                                   : entry.extrude == 0 && entry.sketch == editor.sketch();
        highlight(context, entry.highlight.element(), chosen);
    }
    const auto* active_sketch = services_->document.state().sketch(editor.sketch());
    extrude_button.enabled(active_sketch && !model::find_loops(*active_sketch).closed.empty());
    if (!sketch_info.valid()) { return; }
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
    if (sketch_info.text() != info) { sketch_info.text(info); }
    context.request_frame();
}

} // namespace studio::cad
