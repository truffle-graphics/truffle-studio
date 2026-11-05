#pragma once

#include "truffle/gui/text.hpp"
#include "truffle/gui/widgets.hpp"

#include <string>

namespace studio {

// The Studio GUI: owns the Context and the application's widgets.
class Workspace {
public:
    Workspace(truffle::gui::TextEngine& text, truffle::gui::FontId font,
              truffle::gui::TextureId atlas);

    truffle::gui::Context context;
    truffle::gui::Widget status;
    truffle::gui::Widget action;
    truffle::gui::Widget name;
    int actions = 0;

private:
    truffle::gui::Widget label(truffle::gui::ElementId parent, std::string text);

    truffle::gui::TextEngine& text_;
    truffle::gui::FontId font_;
    truffle::gui::TextureId atlas_;
};

} // namespace studio
