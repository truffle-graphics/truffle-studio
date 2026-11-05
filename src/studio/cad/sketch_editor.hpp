#pragma once

#include "studio/workspace.hpp"

#include "model/sketch.hpp"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace studio::cad {

// The 2D sketch canvas: grid, pan and zoom, snapping and the drawing tools.
// Geometry lives in the document; the editor keeps only view and tool state.
class SketchEditor {
public:
    enum class Tool : std::uint8_t { select, line, rectangle, circle, arc };
    static constexpr std::array tools{Tool::select, Tool::line, Tool::rectangle, Tool::circle,
                                      Tool::arc};
    static const char* tool_name(Tool tool) noexcept;

    // Builds the toolbar and canvas into `parent`, a vertical stack.
    void build(WorkspaceServices& services, truffle::gui::ElementId parent);
    void teardown();
    // Follows document changes (undo can remove the active sketch).
    void sync();

    void set_tool(Tool tool);
    Tool tool() const noexcept { return tool_; }
    model::ObjectId sketch() const noexcept { return sketch_; }
    void set_sketch(model::ObjectId sketch);
    model::ObjectId selection() const noexcept { return selection_; }
    truffle::gui::ElementId canvas() const noexcept { return canvas_; }
    truffle::gui::Widget tool_button(Tool tool) const;

    // Window position of a sketch point, and the reverse.
    truffle::gui::Point to_window(model::Vec2 point) const;
    model::Vec2 to_sketch(truffle::gui::Point window) const;

    // Called when the tool, selection or active sketch changes.
    std::function<void()> changed;

private:
    struct Snap {
        model::Vec2 position;
        model::ObjectId point = 0; // existing sketch point under the cursor
    };

    const model::Sketch* active() const;
    double grid_step() const noexcept;
    Snap snap(truffle::gui::Point window) const;
    void handle(truffle::gui::Context& context, truffle::gui::RoutedEvent& event);
    void click(const Snap& at);
    void pick(model::Vec2 at);
    void commit(std::string label, const std::function<void(model::DocumentState&, model::Sketch&)>& draw);
    void cancel();
    void redraw();
    void notify();
    void paint(truffle::gui::ShapeWriter& out, truffle::gui::Size size) const;
    void refresh_tools();

    WorkspaceServices* services_ = nullptr;
    truffle::gui::ElementId canvas_;
    std::vector<std::pair<truffle::gui::ElementId, truffle::gui::Widget>> tool_buttons_;

    Tool tool_ = Tool::line;
    model::ObjectId sketch_ = 0;
    model::ObjectId selection_ = 0;
    std::vector<model::Vec2> pending_; // clicks of the shape being drawn
    std::optional<Snap> hover_;

    model::Vec2 center_;   // sketch point at the canvas centre
    double scale_ = 4;     // pixels per millimetre
    std::optional<truffle::gui::Point> pan_from_;
    model::Vec2 pan_center_;
};

} // namespace studio::cad
