#pragma once

#include "truffle/gui/graphics.hpp"

#include <optional>

namespace studio {

struct MenuReadbackRegions {
    float scale = 1.0F;
    truffle::gui::Rect bar;
    truffle::gui::Rect file_trigger;
    truffle::gui::Rect edit_trigger;
    std::optional<truffle::gui::Rect> popup;
    std::optional<truffle::gui::Rect> popup_item;
};

void verify_menu_pixels(truffle::gui::Graphics& graphics, truffle::gui::Context& context,
                        truffle::gui::PixelSize pixels, truffle::gui::Color clear,
                        const MenuReadbackRegions& regions);

} // namespace studio
