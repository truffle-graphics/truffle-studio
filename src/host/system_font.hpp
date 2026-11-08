#pragma once

#include "truffle/gui/text.hpp"

namespace studio {

// Loads the platform UI font and an ordered fallback chain into the engine.
// Font discovery is host policy; Truffle only receives font bytes.
truffle::gui::FontId load_system_font(
    truffle::gui::TextEngine& engine, float size = truffle::gui::default_font_size);

} // namespace studio
