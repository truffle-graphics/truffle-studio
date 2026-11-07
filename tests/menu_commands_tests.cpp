#include "studio/menu_commands.hpp"
#include "test.hpp"

#include <SDL3/SDL_keycode.h>

using studio::MenuCommand;
using studio::ShortcutPlatform;
namespace gui = truffle::gui;

namespace {
gui::KeyEvent key(gui::Key physical, SDL_Keycode logical, gui::Modifiers modifiers,
                  bool down = true) {
    return {.key = physical, .down = down, .modifiers = modifiers, .native = logical};
}
} // namespace

TEST(menu_shortcuts_match_each_platform) {
    using gui::Key;
    using gui::Modifiers;
    using studio::menu_command_for_shortcut;
    CHECK(menu_command_for_shortcut(key(Key::q, SDLK_Q, Modifiers::super), ShortcutPlatform::macos)
          == MenuCommand::quit);
    CHECK(menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::super), ShortcutPlatform::macos)
          == MenuCommand::undo);
    CHECK(menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::super | Modifiers::shift),
                                    ShortcutPlatform::macos) == MenuCommand::redo);
    CHECK(menu_command_for_shortcut(key(Key::q, SDLK_Q, Modifiers::control), ShortcutPlatform::windows)
          == MenuCommand::quit);
    CHECK(menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::control), ShortcutPlatform::windows)
          == MenuCommand::undo);
    CHECK(menu_command_for_shortcut(key(Key::y, SDLK_Y, Modifiers::control), ShortcutPlatform::windows)
          == MenuCommand::redo);
    CHECK(menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::control | Modifiers::shift),
                                    ShortcutPlatform::windows) == MenuCommand::redo);
    CHECK(menu_command_for_shortcut(key(Key::a, SDLK_Q, Modifiers::super), ShortcutPlatform::macos)
          == MenuCommand::quit);
    CHECK(menu_command_for_shortcut(key(Key::y, SDLK_Z, Modifiers::control), ShortcutPlatform::windows)
          == MenuCommand::undo);
    CHECK(!menu_command_for_shortcut(key(Key::q, SDLK_A, Modifiers::super), ShortcutPlatform::macos));
    CHECK(!menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::control), ShortcutPlatform::macos));
    CHECK(!menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::super), ShortcutPlatform::windows));
    CHECK(!menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::super | Modifiers::alt),
                                     ShortcutPlatform::macos));
    CHECK(!menu_command_for_shortcut(key(Key::z, SDLK_Z, Modifiers::super, false),
                                     ShortcutPlatform::macos));
    CHECK(studio::menu_shortcut_label(MenuCommand::undo, ShortcutPlatform::macos) == "Cmd+Z");
    CHECK(studio::menu_shortcut_label(MenuCommand::redo, ShortcutPlatform::macos) == "Cmd+Shift+Z");
    CHECK(studio::menu_shortcut_label(MenuCommand::undo, ShortcutPlatform::windows) == "Ctrl+Z");
    CHECK(studio::menu_shortcut_label(MenuCommand::redo, ShortcutPlatform::windows) == "Ctrl+Y");
}

TEST(menu_commands_use_document_history_and_quit_action) {
    studio::model::Document document;
    int quits = 0;
    const auto quit = [&] { ++quits; };
    CHECK(!studio::invoke_menu_command(MenuCommand::undo, document, quit));
    CHECK(document.edit("rename", [](studio::model::DocumentState& state) { state.name = "Part"; }));
    CHECK(studio::invoke_menu_command(MenuCommand::undo, document, quit));
    CHECK(document.state().name == "Untitled");
    CHECK(studio::invoke_menu_command(MenuCommand::redo, document, quit));
    CHECK(document.state().name == "Part");
    CHECK(studio::invoke_menu_command(MenuCommand::quit, document, quit));
    CHECK(quits == 1);
}
