#pragma once

#include "model/document.hpp"
#include "studio/check.hpp"

#include "truffle/gui/dock/dock.hpp"
#include "truffle/gui/ui.hpp"

#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace studio {

class SceneView;

// How the engine is configured while a workspace is active. Each workspace
// states what it is optimised for; the app applies the profile on switch.
struct EngineProfile {
    std::string name;
    std::array<float, 4> clear{0.05F, 0.05F, 0.06F, 1};
    bool depth = false;      // the workspace renders 3D content
    bool continuous = false; // redraw every frame (playback, simulation)
};

// What the shell gives a workspace while it is active.
struct WorkspaceServices {
    truffle::gui::Ui ui;
    model::Document& document;
    std::function<void(std::string)> report;
    SceneView* scene = nullptr; // offscreen 3D rendering; null without a GPU
};

// Marks the active choice in a group of toggles with the theme accent.
inline void highlight(truffle::gui::Context& context, truffle::gui::ElementId element,
                      bool selected) {
    need(context.bind_property(element, truffle::gui::Property::fill, "gui.accent"));
    if (selected) {
        need(context.clear_property_override(element, truffle::gui::Property::fill));
    } else {
        need(context.set_property_override(element, truffle::gui::Property::fill,
                                           truffle::gui::Color{}));
    }
}

// A mode of the application over the shared document. The shell builds the
// active workspace's panels into a fresh dock and tears them down on switch;
// document state lives in the Document, never in panels.
class Workspace {
public:
    Workspace() = default;
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    virtual ~Workspace() = default;

    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view title() const noexcept = 0;
    virtual bool available() const noexcept { return true; }
    virtual EngineProfile profile() const = 0;

    // Creates panels, adds them to `dock` and sets its default layout.
    virtual void build(WorkspaceServices& services, truffle::gui::dock::DockSpace& dock) = 0;
    // Called before the dock is destroyed; drop widget handles here.
    virtual void teardown() {}
    // Once per loop turn, outside input dispatch.
    virtual void update() {}
    // After layout, before the frame is recorded: may request 3D passes and
    // update decorations that depend on element sizes.
    virtual void before_frame() {}
    // True while a workspace text field owns keyboard editing shortcuts.
    virtual bool text_editing(const truffle::gui::Context&) const { return false; }
};

// A workspace that is planned but not built yet; shown disabled.
class PlannedWorkspace final : public Workspace {
public:
    PlannedWorkspace(std::string id, std::string title)
        : id_(std::move(id)), title_(std::move(title)) {}
    std::string_view id() const noexcept override { return id_; }
    std::string_view title() const noexcept override { return title_; }
    bool available() const noexcept override { return false; }
    EngineProfile profile() const override { return {.name = title_}; }
    void build(WorkspaceServices&, truffle::gui::dock::DockSpace&) override {}

private:
    std::string id_;
    std::string title_;
};

} // namespace studio
