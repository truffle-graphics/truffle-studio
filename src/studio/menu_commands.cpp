#include "studio/menu_commands.hpp"

#include <SDL3/SDL_keycode.h>

#include <stdexcept>

namespace studio {
namespace gui = truffle::gui;

std::optional<MenuCommand> menu_command_for_shortcut(const gui::KeyEvent& event,
                                                     ShortcutPlatform platform) {
    if (!event.down) { return std::nullopt; }

    gui::Modifiers primary;
    switch (platform) {
    case ShortcutPlatform::macos: primary = gui::Modifiers::super; break;
    case ShortcutPlatform::windows: primary = gui::Modifiers::control; break;
    default: throw std::invalid_argument("unknown shortcut platform");
    }

    if (event.modifiers == primary) {
        if (event.native == SDLK_Q) { return MenuCommand::quit; }
        if (event.native == SDLK_Z) { return MenuCommand::undo; }
        if (platform == ShortcutPlatform::windows && event.native == SDLK_Y) {
            return MenuCommand::redo;
        }
    }
    if (event.modifiers == (primary | gui::Modifiers::shift) && event.native == SDLK_Z) {
        return MenuCommand::redo;
    }
    return std::nullopt;
}

std::string_view menu_shortcut_label(MenuCommand command, ShortcutPlatform platform) {
    switch (platform) {
    case ShortcutPlatform::macos:
        switch (command) {
        case MenuCommand::quit: return "Cmd+Q";
        case MenuCommand::undo: return "Cmd+Z";
        case MenuCommand::redo: return "Cmd+Shift+Z";
        }
        break;
    case ShortcutPlatform::windows:
        switch (command) {
        case MenuCommand::quit: return "Ctrl+Q";
        case MenuCommand::undo: return "Ctrl+Z";
        case MenuCommand::redo: return "Ctrl+Y";
        }
        break;
    }
    throw std::invalid_argument("unknown menu command or shortcut platform");
}

bool invoke_menu_command(MenuCommand command, model::Document& document,
                         const std::function<void()>& quit) {
    switch (command) {
    case MenuCommand::quit:
        quit();
        return true;
    case MenuCommand::undo: return document.undo();
    case MenuCommand::redo: return document.redo();
    }
    throw std::invalid_argument("unknown menu command");
}

} // namespace studio
