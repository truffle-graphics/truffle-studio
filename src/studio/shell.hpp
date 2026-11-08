#pragma once

#include "model/document.hpp"
#include "studio/workspace.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace studio {

// The application window's GUI: a top bar with the workspace switcher and
// document commands, the active workspace's dock, and a status line.
class Shell {
public:
    Shell(model::Document& document, truffle::gui::TextEngine& text, truffle::gui::FontId font,
          truffle::gui::TextureId atlas, SceneView* scene = nullptr);
    ~Shell();
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    void add(std::unique_ptr<Workspace> workspace);
    // Switches immediately; use request() from input handlers.
    void activate(std::string_view id);
    void request(std::string_view id);

    // Applies deferred requests and updates the active workspace.
    void update();
    // Lays out a frame that is due and lets the active workspace prepare it.
    void before_frame();

    void undo();
    void redo();
    bool text_editing() const;
    void report(std::string message);

    Workspace* active() const noexcept { return active_; }
    Workspace* find(std::string_view id) const noexcept;
    truffle::gui::Button switcher(std::string_view id) const;
    const std::string& default_layout() const;

    truffle::gui::Context context;
    truffle::gui::dock::DockSpace dock;
    truffle::gui::Button undo_button;
    truffle::gui::Button redo_button;
    truffle::gui::Button reset_layout;
    truffle::gui::Label status;

private:
    struct Entry {
        std::unique_ptr<Workspace> workspace;
        truffle::gui::Container highlight;
        truffle::gui::Button button;
        std::string layout;         // last layout, kept across switches
        std::string default_layout; // as built
    };
    Entry* entry(std::string_view id) noexcept;
    const Entry* entry(std::string_view id) const noexcept;
    void close_active();

    model::Document& document_;
    truffle::gui::Ui ui_;
    WorkspaceServices services_;
    truffle::gui::Container switcher_bar_;
    truffle::gui::Container content_;
    std::vector<Entry> entries_;
    Workspace* active_ = nullptr;
    std::string requested_;
    bool reset_requested_ = false;
};

} // namespace studio
