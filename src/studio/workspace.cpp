#include "studio/workspace.hpp"

#include "studio/check.hpp"

#include <memory>

namespace studio {
namespace gui = truffle::gui;

Workspace::Workspace(gui::TextEngine& text, gui::FontId font, gui::TextureId atlas)
    : text_(text), font_(font), atlas_(atlas) {
    const auto root = context.root();
    need(context.set_layout(root, std::make_shared<gui::StackLayout>(
        gui::StackOptions{.spacing = 10})));
    gui::LayoutParams params;
    params.padding = {16, 16, 16, 16};
    need(context.set_layout_params(root, params));
    need(context.set_decoration(root, {.fill = {0.09F, 0.09F, 0.10F, 1}}));

    static_cast<void>(label(root, "Truffle Studio"));
    status = label(root, "Ready");
    action = need(gui::Widget::create(context, root, {
        .kind = gui::WidgetKind::button, .text = "Run action",
        .text_engine = &text_, .font = font_, .atlas = atlas_,
        .activated = [this] {
            ++actions;
            need(status.set_text("Actions run: " + std::to_string(actions)));
        }}));
    name = need(gui::Widget::create(context, root, {
        .kind = gui::WidgetKind::text_edit, .text = "Untitled",
        .text_engine = &text_, .font = font_, .atlas = atlas_}));
}

gui::Widget Workspace::label(gui::ElementId parent, std::string value) {
    return need(gui::Widget::create(context, parent, {
        .kind = gui::WidgetKind::label, .text = std::move(value),
        .text_engine = &text_, .font = font_, .atlas = atlas_}));
}

} // namespace studio
