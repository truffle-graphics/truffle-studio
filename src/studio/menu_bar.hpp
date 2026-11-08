#pragma once

#include "truffle/gui/ui.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace studio {
namespace model { class Document; }

// Adds the bar under `ui.root()`. The Context, document and the quit
// callback's captures outlive this menu bar.
class MainMenu {
public:
    MainMenu(const truffle::gui::Ui& ui, model::Document& document, std::function<void()> quit);
    ~MainMenu();
    MainMenu(const MainMenu&) = delete;
    MainMenu& operator=(const MainMenu&) = delete;

    void sync();
    [[nodiscard]] truffle::gui::ElementId element() const noexcept { return bar_.element(); }
    [[nodiscard]] const truffle::gui::Menu& file() const noexcept;
    [[nodiscard]] const truffle::gui::Menu& edit() const noexcept;
    [[nodiscard]] truffle::gui::ElementId quit_item() const noexcept;
    [[nodiscard]] truffle::gui::ElementId undo_item() const noexcept;
    [[nodiscard]] truffle::gui::ElementId redo_item() const noexcept;

private:
    struct State;
    model::Document& document_;
    truffle::gui::MenuBar bar_;
    std::shared_ptr<State> state_;
    std::uint64_t subscription_ = 0;
};

} // namespace studio
