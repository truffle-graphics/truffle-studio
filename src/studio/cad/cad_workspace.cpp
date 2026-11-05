#include "studio/cad/cad_workspace.hpp"

#include "model/sketch_edit.hpp"

namespace studio::cad {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

namespace {
constexpr gui::Color selected{0.24F, 0.45F, 0.85F, 1};

std::string plural(std::size_t count, const char* noun) {
    return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
}
} // namespace

EngineProfile CadWorkspace::profile() const {
    // Precise, on-demand rendering: frames only follow edits and navigation.
    return {.name = "CAD", .clear = {0.11F, 0.12F, 0.14F, 1}, .depth = true, .continuous = false};
}

void CadWorkspace::build(WorkspaceServices& services, dock::DockSpace& space) {
    services_ = &services;
    const auto& ui = services.ui;

    const auto model = ui.column(10);
    static_cast<void>(ui.label(model, "Sketches"));
    sketch_list_ = need(ui.context.create(model));
    need(ui.context.set_layout(sketch_list_, std::make_shared<gui::StackLayout>(gui::StackOptions{.spacing = 2})));
    const auto planes = ui.row(model, 4);
    static_cast<void>(ui.label(planes, "New sketch on"));
    for (const auto plane : {model::SketchPlane::xy, model::SketchPlane::xz, model::SketchPlane::yz}) {
        auto button = ui.button(planes, model::plane_name(plane), [this, plane] {
            model::ObjectId created = 0;
            if (services_->document.edit("New sketch", [&](model::DocumentState& state) {
                    created = model::add_sketch(state, plane);
                })) {
                editor.set_sketch(created);
            }
        });
        if (plane == model::SketchPlane::xy) { new_sketch = button; }
    }

    const auto viewport = ui.column(8);
    editor.changed = [this] { refresh(); };
    editor.build(services, viewport);

    const auto inspector = ui.column(12);
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

    need(space.add_panel("model", "Model", model));
    need(space.add_panel("viewport", "Viewport", viewport));
    need(space.add_panel("inspector", "Inspector", inspector));
    need(space.set_layout(dock::DockNode::split(dock::Axis::horizontal, {
        dock::DockNode::tabs({"model"}),
        dock::DockNode::tabs({"viewport"}),
        dock::DockNode::tabs({"inspector"})}, {0.2F, 0.58F, 0.22F})));
    subscription_ = services.document.subscribe([this] { sync(); });
    rebuild_sketch_list();
    refresh();
}

void CadWorkspace::teardown() {
    if (services_) { services_->document.unsubscribe(subscription_); }
    editor.teardown();
    editor.changed = {};
    part_name = {};
    sketch_info = {};
    new_sketch = {};
    sketch_buttons.clear();
    entries_.clear();
    listed_.clear();
    sketch_list_ = {};
    services_ = nullptr;
}

void CadWorkspace::update() {
    // The list is rebuilt here, not in sync(): a button that changes the
    // document must not be destroyed while its click is dispatched.
    if (services_ && sketch_signature() != listed_) { rebuild_sketch_list(); }
    editor.update();
}

bool CadWorkspace::text_editing(const gui::Context& context) const {
    return (part_name.alive() && context.focused() == part_name.element()) || editor.editing_dimension(context);
}

void CadWorkspace::sync() {
    const auto& value = services_->document.state().name;
    if (part_name.alive() && need(part_name.text()) != value) { need(part_name.set_text(value)); }
    editor.sync();
    services_->ui.context.request_frame();
}

std::string CadWorkspace::sketch_signature() const {
    std::string signature;
    for (const auto& sketch : services_->document.state().sketches) {
        signature += std::to_string(sketch.id) + ':' + sketch.name + ':' + model::plane_name(sketch.plane) + ';';
    }
    return signature;
}

void CadWorkspace::rebuild_sketch_list() {
    auto& context = services_->ui.context;
    for (const auto& entry : entries_) { need(context.destroy(entry.highlight)); }
    entries_.clear();
    sketch_buttons.clear();
    for (const auto& sketch : services_->document.state().sketches) {
        const auto highlight = services_->ui.row(sketch_list_, 0, {2, 2, 2, 2});
        const auto id = sketch.id;
        sketch_buttons.push_back(services_->ui.button(
            highlight, sketch.name + " (" + model::plane_name(sketch.plane) + ")",
            [this, id] { editor.set_sketch(id); }));
        entries_.push_back({highlight, id});
    }
    listed_ = sketch_signature();
    refresh();
}

void CadWorkspace::refresh() {
    if (!services_) { return; }
    auto& context = services_->ui.context;
    for (const auto& entry : entries_) {
        const auto fill = entry.sketch == editor.sketch() ? selected : gui::Color{};
        need(context.set_decoration(entry.highlight, {.fill = fill, .corner_radius = 4}));
    }
    if (!sketch_info.alive()) { return; }
    std::string info;
    if (const auto* sketch = services_->document.state().sketch(editor.sketch())) {
        const auto loops = model::find_loops(*sketch);
        info = sketch->name + " on " + model::plane_name(sketch->plane) + "\n" +
               plural(sketch->curve_count(), "curve") + ", " + plural(sketch->points.size(), "point") + "\n" +
               plural(loops.closed.size(), "closed profile");
        if (loops.open) { info += ", " + plural(loops.open, "open chain"); }
        const auto& status = editor.status();
        info += "\n" + plural(sketch->constraints.size(), "constraint") + "\n";
        switch (status.status) {
        case model::SolveStatus::full: info += "Fully constrained"; break;
        case model::SolveStatus::under: info += "Under-constrained (" + std::to_string(status.freedom) + " DOF)"; break;
        case model::SolveStatus::over: info += "Over-constrained"; break;
        }
        if (!editor.selection().empty()) { info += "\n" + plural(editor.selection().size(), "item") + " selected"; }
    } else {
        info = "No sketch yet. Draw in the viewport\nto start one on the XY plane.";
    }
    info += std::string("\nTool: ") + SketchEditor::tool_name(editor.tool());
    if (need(sketch_info.text()) != info) { need(sketch_info.set_text(info)); }
    context.request_frame();
}

} // namespace studio::cad
