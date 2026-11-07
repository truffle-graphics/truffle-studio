#pragma once

#include "truffle/gui/application.hpp"

namespace studio {

void verify_clear_pixels(truffle::gui::Application& application,
                         truffle::gui::PixelSize pixels,
                         truffle::gui::Color clear);

} // namespace studio
