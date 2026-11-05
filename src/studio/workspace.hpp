#pragma once

#include "model/document.hpp"

#include "truffle/gui/dock/dock.hpp"
#include "truffle/gui/text.hpp"
#include "truffle/gui/widgets.hpp"

#include <string>

namespace studio {

// The Studio GUI: a toolbar, a dock of workspace panels and a status line.
// Owns the Context; panel contents are placeholders until Truffle provides
// the scene and 3D viewport capabilities they need.
class Workspace {
public:
    Workspace(model::Document& document, truffle::gui::TextEngine& text,
              truffle::gui::FontId font, truffle::gui::TextureId atlas);
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    // Applies requests deferred out of input handlers. Call once per loop turn.
    void update();

    // Document undo/redo; reported on the status line.
    void undo();
    void redo();
    // True while a text field has keyboard focus and owns editing shortcuts.
    bool text_editing() const noexcept;

    truffle::gui::Context context;
    truffle::gui::dock::DockSpace dock;
    truffle::gui::Widget undo_button;
    truffle::gui::Widget redo_button;
    truffle::gui::Widget reset_layout;
    truffle::gui::Widget status;
    truffle::gui::Widget name;
    truffle::gui::Widget visible;
    truffle::gui::Widget opacity;
    std::string default_layout;

private:
    truffle::gui::Widget label(truffle::gui::ElementId parent, std::string text);
    truffle::gui::ElementId panel(float padding);
    void report(std::string message);

    void sync_from_document();

    model::Document& document_;
    truffle::gui::TextEngine& text_;
    truffle::gui::FontId font_;
    truffle::gui::TextureId atlas_;
    bool reset_requested_ = false;
};

} // namespace studio
