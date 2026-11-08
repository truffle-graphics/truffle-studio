#pragma once

#include "model/document.hpp"
#include "truffle/gui/input.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace studio {

enum class MenuCommand { quit, undo, redo };
enum class ShortcutPlatform { macos, windows };

// Studio's SDL host places the layout-aware key code in KeyEvent::native.
[[nodiscard]] std::optional<MenuCommand>
menu_command_for_shortcut(const truffle::gui::KeyEvent& event, ShortcutPlatform platform);
[[nodiscard]] std::string_view menu_shortcut_label(MenuCommand command,
                                                   ShortcutPlatform platform);
[[nodiscard]] bool invoke_menu_command(MenuCommand command, model::Document& document,
                                       const std::function<void()>& quit);

} // namespace studio
