#pragma once

#include "studio/check.hpp"

#include "truffle/gui/text.hpp"
#include "truffle/gui/widgets.hpp"

#include <functional>
#include <memory>
#include <string>

namespace studio {

// Widget construction shared by the shell and the workspaces.
struct Ui {
    truffle::gui::Context& context;
    truffle::gui::TextEngine& text;
    truffle::gui::FontId font;
    truffle::gui::TextureId atlas;

    truffle::gui::Widget widget(truffle::gui::ElementId parent, truffle::gui::WidgetDesc desc) const {
        desc.text_engine = &text;
        desc.font = font;
        desc.atlas = atlas;
        return need(truffle::gui::Widget::create(context, parent, std::move(desc)));
    }
    truffle::gui::Widget label(truffle::gui::ElementId parent, std::string value) const {
        return widget(parent, {.kind = truffle::gui::WidgetKind::label, .text = std::move(value)});
    }
    truffle::gui::Widget button(truffle::gui::ElementId parent, std::string value,
                                std::function<void()> activated) const {
        return widget(parent, {.kind = truffle::gui::WidgetKind::button, .text = std::move(value),
                               .activated = std::move(activated)});
    }
    void highlight(truffle::gui::ElementId element, bool selected) const {
        need(context.bind_property(element, truffle::gui::Property::fill, "gui.accent"));
        if (selected) {
            need(context.clear_property_override(element, truffle::gui::Property::fill));
        } else {
            need(context.set_property_override(element, truffle::gui::Property::fill, truffle::gui::Color{}));
        }
    }
    // A horizontal stack under `parent`.
    truffle::gui::ElementId row(truffle::gui::ElementId parent, float spacing,
                                truffle::gui::Edges padding = {}) const {
        const auto element = need(context.create(parent));
        need(context.set_layout(element, std::make_shared<truffle::gui::StackLayout>(
            truffle::gui::StackOptions{.axis = truffle::gui::Axis::horizontal, .spacing = spacing})));
        truffle::gui::LayoutParams params;
        params.padding = padding;
        need(context.set_layout_params(element, params));
        return element;
    }
    // A vertical stack, created under the root for handing to a dock.
    truffle::gui::ElementId column(float padding, float spacing = 8) const {
        const auto element = need(context.create(context.root()));
        need(context.set_layout(element, std::make_shared<truffle::gui::StackLayout>(
            truffle::gui::StackOptions{.spacing = spacing})));
        truffle::gui::LayoutParams params;
        params.padding = {padding, padding, padding, padding};
        need(context.set_layout_params(element, params));
        return element;
    }
};

} // namespace studio
