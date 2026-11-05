#include "studio/cad/cad_workspace.hpp"

namespace studio::cad {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

EngineProfile CadWorkspace::profile() const {
    // Precise, on-demand rendering: frames only follow edits and navigation.
    return {.name = "CAD", .clear = {0.11F, 0.12F, 0.14F, 1}, .depth = true, .continuous = false};
}

void CadWorkspace::build(WorkspaceServices& services, dock::DockSpace& space) {
    services_ = &services;
    const auto& ui = services.ui;

    const auto model = ui.column(10);
    static_cast<void>(ui.label(model, "Model"));
    static_cast<void>(ui.label(model, "No sketches yet."));

    const auto viewport = ui.column(16);
    need(ui.context.set_decoration(viewport, {.fill = {0.11F, 0.12F, 0.14F, 1}}));
    static_cast<void>(ui.label(viewport, "Sketch plane"));

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

    need(space.add_panel("model", "Model", model));
    need(space.add_panel("viewport", "Viewport", viewport));
    need(space.add_panel("inspector", "Inspector", inspector));
    need(space.set_layout(dock::DockNode::split(dock::Axis::horizontal, {
        dock::DockNode::tabs({"model"}),
        dock::DockNode::tabs({"viewport"}),
        dock::DockNode::tabs({"inspector"})}, {0.2F, 0.58F, 0.22F})));
    subscription_ = services.document.subscribe([this] { sync(); });
}

void CadWorkspace::teardown() {
    if (services_) { services_->document.unsubscribe(subscription_); }
    part_name = {};
    services_ = nullptr;
}

bool CadWorkspace::text_editing(const gui::Context& context) const {
    return part_name.alive() && context.focused() == part_name.element();
}

void CadWorkspace::sync() {
    const auto& value = services_->document.state().name;
    if (part_name.alive() && need(part_name.text()) != value) { need(part_name.set_text(value)); }
}

} // namespace studio::cad
