#include "studio/workspace.hpp"

#include "studio/check.hpp"

#include <cmath>
#include <memory>

namespace studio {
namespace gui = truffle::gui;
namespace dock = truffle::gui::dock;

Workspace::Workspace(model::Document& document, gui::TextEngine& text, gui::FontId font,
                     gui::TextureId atlas)
    : document_(document), text_(text), font_(font), atlas_(atlas) {
    const auto root = context.root();
    need(context.set_layout(root, std::make_shared<gui::StackLayout>()));
    need(context.set_decoration(root, {.fill = {0.07F, 0.07F, 0.08F, 1}}));

    const auto toolbar = need(context.create(root));
    need(context.set_layout(toolbar, std::make_shared<gui::StackLayout>(
        gui::StackOptions{.axis = gui::Axis::horizontal, .spacing = 12})));
    gui::LayoutParams params;
    params.padding = {10, 6, 10, 6};
    need(context.set_layout_params(toolbar, params));
    need(context.set_decoration(toolbar, {.fill = {0.10F, 0.10F, 0.11F, 1}}));
    static_cast<void>(label(toolbar, "Truffle Studio"));
    undo_button = need(gui::Widget::create(context, toolbar, {
        .kind = gui::WidgetKind::button, .text = "Undo",
        .text_engine = &text_, .font = font_, .atlas = atlas_,
        .activated = [this] { undo(); }}));
    redo_button = need(gui::Widget::create(context, toolbar, {
        .kind = gui::WidgetKind::button, .text = "Redo",
        .text_engine = &text_, .font = font_, .atlas = atlas_,
        .activated = [this] { redo(); }}));
    reset_layout = need(gui::Widget::create(context, toolbar, {
        .kind = gui::WidgetKind::button, .text = "Reset layout",
        .text_engine = &text_, .font = font_, .atlas = atlas_,
        // Restoring reparents panels, so it waits until input dispatch ends.
        .activated = [this] {
            reset_requested_ = true;
            context.request_frame();
        }}));

    dock = need(dock::DockSpace::create(context, root, {
        .text_engine = &text_, .font = font_, .atlas = atlas_}));
    params = {};
    params.weight = 1;
    need(context.set_layout_params(dock.element(), params));

    const auto scene = panel(10);
    for (const char* item : {"Camera", "Key light", "Cube"}) {
        static_cast<void>(label(scene, item));
    }

    const auto viewport = panel(16);
    need(context.set_decoration(viewport, {.fill = {0.10F, 0.13F, 0.18F, 1}}));
    static_cast<void>(label(viewport, "Viewport"));
    static_cast<void>(label(viewport, "3D rendering is not available yet."));

    const auto properties = panel(12);
    static_cast<void>(label(properties, "Name"));
    name = need(gui::Widget::create(context, properties, {
        .kind = gui::WidgetKind::text_edit, .text = document_.state().name,
        .text_engine = &text_, .font = font_, .atlas = atlas_,
        .text_changed = [this](const std::string& value) {
            if (document_.edit("Rename", [&](model::DocumentState& state) { state.name = value; })) {
                report("Name: " + value);
            }
        }}));
    visible = need(gui::Widget::create(context, properties, {
        .kind = gui::WidgetKind::checkbox, .text = "Visible",
        .text_engine = &text_, .font = font_, .atlas = atlas_, .checked = true,
        .toggled = [this](bool on) { report(on ? "Cube shown" : "Cube hidden"); }}));
    static_cast<void>(label(properties, "Opacity"));
    opacity = need(gui::Widget::create(context, properties, {
        .kind = gui::WidgetKind::slider, .text = "Opacity", .value = 1, .step = 0.05F,
        .value_changed = [this](float value) {
            report("Opacity: " + std::to_string(static_cast<int>(std::lround(value * 100))) + "%");
        }}));

    need(dock.add_panel("scene", "Scene", scene));
    need(dock.add_panel("viewport", "Viewport", viewport));
    need(dock.add_panel("properties", "Properties", properties));
    need(dock.set_layout(dock::DockNode::split(dock::Axis::horizontal, {
        dock::DockNode::tabs({"scene"}),
        dock::DockNode::tabs({"viewport"}),
        dock::DockNode::tabs({"properties"})}, {0.2F, 0.55F, 0.25F})));
    default_layout = dock.save();

    status = label(root, "Ready");
    document_.subscribe([this] { sync_from_document(); });
    params = {};
    params.padding = {10, 4, 10, 4};
    need(context.set_layout_params(status.element(), params));
}

Workspace::~Workspace() {
    if (dock.alive()) { static_cast<void>(dock.shutdown()); }
}

void Workspace::update() {
    if (reset_requested_) {
        reset_requested_ = false;
        need(dock.restore(default_layout));
        report("Layout reset");
    }
}

void Workspace::undo() {
    const auto label = document_.undo_label();
    report(document_.undo() ? "Undo " + label : "Nothing to undo");
}

void Workspace::redo() {
    const auto label = document_.redo_label();
    report(document_.redo() ? "Redo " + label : "Nothing to redo");
}

bool Workspace::text_editing() const noexcept {
    return name.alive() && context.focused() == name.element();
}

void Workspace::sync_from_document() {
    const auto& value = document_.state().name;
    if (need(name.text()) != value) { need(name.set_text(value)); }
}

gui::Widget Workspace::label(gui::ElementId parent, std::string value) {
    return need(gui::Widget::create(context, parent, {
        .kind = gui::WidgetKind::label, .text = std::move(value),
        .text_engine = &text_, .font = font_, .atlas = atlas_}));
}

gui::ElementId Workspace::panel(float padding) {
    const auto element = need(context.create(context.root()));
    need(context.set_layout(element, std::make_shared<gui::StackLayout>(
        gui::StackOptions{.spacing = 8})));
    gui::LayoutParams params;
    params.padding = {padding, padding, padding, padding};
    need(context.set_layout_params(element, params));
    return element;
}

void Workspace::report(std::string message) {
    need(status.set_text(std::move(message)));
}

} // namespace studio
