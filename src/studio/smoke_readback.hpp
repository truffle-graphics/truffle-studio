#pragma once

#include "truffle/gui/graphics.hpp"

namespace studio {

void verify_clear_pixels(truffle::gui::Graphics& graphics,
                         truffle::gui::PixelSize pixels,
                         truffle::gui::Color clear);

} // namespace studio
