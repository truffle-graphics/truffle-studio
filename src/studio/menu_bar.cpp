#include "studio/menu_bar.hpp"

#include "studio/check.hpp"
#include "studio/menu_commands.hpp"

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

    State(model::Document& value, std::function<void()> on_quit)
        : document(value), quit(std::move(on_quit)) {}

    void sync() {
        undo_item.enabled(document.can_undo());
        redo_item.enabled(document.can_redo());
    }

    void invoke(MenuCommand command) {
        static_cast<void>(invoke_menu_command(command, document, quit));
        sync();
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
            if (menu->is_open()) { menu->close(); }
        }
        invoke(*command);
    }
};

MainMenu::MainMenu(const gui::Ui& ui, model::Document& document, std::function<void()> quit)
    : document_(document), state_(std::make_shared<State>(document, std::move(quit))) {
    if (!state_->quit) { throw std::invalid_argument("the Studio menu needs a quit action"); }

    bar_ = ui.root().menu_bar();
    state_->file = bar_.menu("File");
    state_->edit = bar_.menu("Edit");
    const std::weak_ptr<State> weak = state_;
    const auto action = [weak](MenuCommand command) {
        return [weak, command] {
            if (const auto state = weak.lock()) { state->invoke(command); }
        };
    };
    const auto shortcut = [](MenuCommand command) {
        return std::string(menu_shortcut_label(command, shortcut_platform));
    };
    state_->quit_item = state_->file.item("Quit")
        .shortcut(shortcut(MenuCommand::quit)).on_click(action(MenuCommand::quit));
    state_->undo_item = state_->edit.item("Undo")
        .shortcut(shortcut(MenuCommand::undo)).on_click(action(MenuCommand::undo));
    state_->redo_item = state_->edit.item("Redo")
        .shortcut(shortcut(MenuCommand::redo)).on_click(action(MenuCommand::redo));
    state_->sync();
    need(ui.context().set_input_filter(bar_.element(),
        [weak](gui::Context& current, gui::RoutedEvent& event) {
            if (const auto state = weak.lock()) { state->filter(current, event); }
        }));
    subscription_ = document_.subscribe([weak] {
        if (const auto state = weak.lock()) { state->sync(); }
    });
}

MainMenu::~MainMenu() {
    document_.unsubscribe(subscription_);
    bar_.remove();
}

void MainMenu::sync() { state_->sync(); }
const gui::Menu& MainMenu::file() const noexcept { return state_->file; }
const gui::Menu& MainMenu::edit() const noexcept { return state_->edit; }
gui::ElementId MainMenu::quit_item() const noexcept { return state_->quit_item.element(); }
gui::ElementId MainMenu::undo_item() const noexcept { return state_->undo_item.element(); }
gui::ElementId MainMenu::redo_item() const noexcept { return state_->redo_item.element(); }

} // namespace studio
