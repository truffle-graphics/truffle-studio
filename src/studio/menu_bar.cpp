#include "studio/menu_bar.hpp"

#include "studio/check.hpp"
#include "studio/menu_commands.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace studio {
namespace gui = truffle::gui;
namespace base = truffle::base;

namespace {
#if defined(_WIN32)
constexpr ShortcutPlatform shortcut_platform = ShortcutPlatform::windows;
#else
constexpr ShortcutPlatform shortcut_platform = ShortcutPlatform::macos;
#endif

bool text_edit_focused(const gui::Context& context) {
    auto element = context.focused();
    while (context.alive(element)) {
        if (need(context.semantics(element)).role == gui::Context::Role::text_edit) {
            return true;
        }
        element = need(context.parent(element));
    }
    return false;
}
} // namespace

struct MainMenu::State {
    model::Document& document;
    std::function<void()> quit;
    gui::Menu file;
    gui::Menu edit;
    gui::MenuItem quit_item;
    gui::MenuItem undo_item;
    gui::MenuItem redo_item;
    bool undo_enabled;
    bool redo_enabled;

    State(model::Document& value, std::function<void()> on_quit)
        : document(value), quit(std::move(on_quit)),
          undo_enabled(document.can_undo()), redo_enabled(document.can_redo()) {}

    base::Status sync() {
        const bool can_undo = document.can_undo();
        if (can_undo != undo_enabled) {
            auto status = undo_item.set_enabled(can_undo);
            if (!status.ok()) { return status; }
            undo_enabled = can_undo;
        }
        const bool can_redo = document.can_redo();
        if (can_redo != redo_enabled) {
            auto status = redo_item.set_enabled(can_redo);
            if (!status.ok()) { return status; }
            redo_enabled = can_redo;
        }
        return base::Status::success();
    }

    void invoke(MenuCommand command) {
        static_cast<void>(invoke_menu_command(command, document, quit));
        need(sync());
    }

    void filter(gui::Context& context, gui::RoutedEvent& event) {
        const auto* key = event.input<gui::KeyEvent>();
        if (key == nullptr || !context.input().focused) { return; }
        const auto command = menu_command_for_shortcut(*key, shortcut_platform);
        if (!command || (*command != MenuCommand::quit && text_edit_focused(context))) {
            return;
        }
        event.prevent_default();
        event.stop_propagation();
        if (key->repeat) { return; }
        for (auto* menu : {&file, &edit}) {
            if (menu->is_open()) {
                event.status = menu->close();
                if (!event.status.ok()) { return; }
            }
        }
        invoke(*command);
    }
};

MainMenu::MainMenu(gui::Context& context, gui::MenuTextStyle text,
                   model::Document& document, std::function<void()> quit)
    : context_(context), document_(document), state_(std::make_shared<State>(document, std::move(quit))) {
    if (!state_->quit) { throw std::invalid_argument("the Studio menu needs a quit action"); }

    need(context_.set_layout(context_.root(), std::make_shared<gui::StackLayout>()));
    bar_ = need(gui::MenuBar::create(context_, context_.root(), text));

    state_->file = need(bar_.add_menu("File"));
    state_->edit = need(bar_.add_menu("Edit"));
    const std::weak_ptr<State> weak = state_;
    const auto action = [weak](MenuCommand command) {
        return [weak, command] {
            if (const auto state = weak.lock()) { state->invoke(command); }
        };
    };
    state_->quit_item = need(state_->file.add_item({
        .text = "Quit",
        .shortcut = std::string(menu_shortcut_label(MenuCommand::quit, shortcut_platform)),
        .activated = action(MenuCommand::quit),
    }));
    state_->undo_item = need(state_->edit.add_item({
        .text = "Undo",
        .enabled = state_->undo_enabled,
        .shortcut = std::string(menu_shortcut_label(MenuCommand::undo, shortcut_platform)),
        .activated = action(MenuCommand::undo),
    }));
    state_->redo_item = need(state_->edit.add_item({
        .text = "Redo",
        .enabled = state_->redo_enabled,
        .shortcut = std::string(menu_shortcut_label(MenuCommand::redo, shortcut_platform)),
        .activated = action(MenuCommand::redo),
    }));
    need(context_.set_input_filter(bar_.element(),
        [weak](gui::Context& current, gui::RoutedEvent& event) {
            if (const auto state = weak.lock()) { state->filter(current, event); }
        }));
    subscription_ = document_.subscribe([weak] {
        if (const auto state = weak.lock()) { need(state->sync()); }
    });
}

MainMenu::~MainMenu() {
    document_.unsubscribe(subscription_);
    if (bar_.alive()) {
        const auto status = context_.destroy(bar_.element());
        if (!status.ok()) { std::cerr << "Could not remove Studio menu bar: " << status.message << '\n'; }
    }
}

base::Status MainMenu::sync() { return state_->sync(); }
const gui::Menu& MainMenu::file() const noexcept { return state_->file; }
const gui::Menu& MainMenu::edit() const noexcept { return state_->edit; }
gui::ElementId MainMenu::quit_item() const noexcept { return state_->quit_item.element(); }
gui::ElementId MainMenu::undo_item() const noexcept { return state_->undo_item.element(); }
gui::ElementId MainMenu::redo_item() const noexcept { return state_->redo_item.element(); }

} // namespace studio
