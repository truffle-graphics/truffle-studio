#pragma once

#include "studio/workspace.hpp"

#include "model/sketch.hpp"
#include "model/solver.hpp"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace studio::cad {

// The 2D sketch canvas: grid, pan and zoom, snapping, the drawing tools,
// constraints and editable dimensions. Geometry lives in the document; the
// editor keeps only view, tool and selection state.
class SketchEditor {
public:
    enum class Tool : std::uint8_t { select, line, rectangle, circle, arc };
    static constexpr std::array tools{Tool::select, Tool::line, Tool::rectangle, Tool::circle,
                                      Tool::arc};
    static const char* tool_name(Tool tool) noexcept;

    // Constraint commands applied to the selection.
    enum class Action : std::uint8_t {
        coincident, horizontal, vertical, parallel, perpendicular, equal, fixed, dimension
    };
    static constexpr std::array actions{Action::coincident, Action::horizontal, Action::vertical,
                                        Action::parallel,   Action::perpendicular, Action::equal,
                                        Action::fixed,      Action::dimension};
    static const char* action_name(Action action) noexcept;

    // Drawing tools stay above the canvas; constraints live in the Inspector.
    void build(WorkspaceServices& services, truffle::gui::Container parent,
               truffle::gui::Container inspector);
    void set_visible(bool visible);
    void teardown();
    // Follows document changes (undo can remove the active sketch).
    void sync();
    // Once per loop turn, outside input dispatch: creates, removes and
    // updates the dimension fields.
    void update();
    // Positions dimension fields after their default sizes have been measured.
    void before_frame();

    void set_tool(Tool tool);
    Tool tool() const noexcept { return tool_; }
    model::ObjectId sketch() const noexcept { return sketch_; }
    void set_sketch(model::ObjectId sketch);
    const std::vector<model::ObjectId>& selection() const noexcept { return selection_; }
    truffle::gui::ElementId canvas() const noexcept { return canvas_; }
    truffle::gui::Button tool_button(Tool tool) const;
    truffle::gui::Button action_button(Action action) const;
    // Applies `action` to the selection; false (and a status message) if
    // the selection does not suit it or it would over-constrain.
    bool apply(Action action);
    bool can_apply(Action action) const;
    // How constrained the active sketch is.
    const model::SolveResult& status() const noexcept { return status_; }
    // The field showing a dimension constraint's value.
    truffle::gui::TextField dimension_field(model::ObjectId constraint) const;
    bool editing_dimension(const truffle::gui::Context& context) const;

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
    model::ObjectId hit(model::Vec2 at) const;
    void pick(model::Vec2 at, bool extend);
    void drag_to(model::Vec2 at);
    void submit_dimension(model::ObjectId constraint);
    std::optional<truffle::gui::Point> dimension_anchor(const model::Sketch& sketch,
                                                        const model::SketchConstraint& c,
                                                        truffle::gui::Size field_size) const;
    void commit(std::string label, const std::function<void(model::DocumentState&, model::Sketch&)>& draw);
    void cancel();
    void redraw();
    void notify();
    void paint(truffle::gui::ShapeWriter& out, truffle::gui::Size size) const;
    void refresh_tools();
    struct Request {
        model::ConstraintKind kind;
        model::ObjectId a;
        model::ObjectId b;
    };
    std::vector<Request> requests(Action action) const;

    WorkspaceServices* services_ = nullptr;
    truffle::gui::ElementId canvas_;
    truffle::gui::Container constraints_;
    truffle::gui::Container constraint_grid_;
    std::uint16_t constraint_columns_ = 2;
    bool visible_ = true;
    std::vector<std::pair<truffle::gui::Container, truffle::gui::Button>> tool_buttons_;
    std::vector<truffle::gui::Button> action_buttons_;
    struct Field {
        truffle::gui::TextField widget;
        truffle::gui::Point position;
    };
    std::map<model::ObjectId, Field> fields_; // by dimension constraint id

    Tool tool_ = Tool::line;
    model::ObjectId sketch_ = 0;
    std::vector<model::ObjectId> selection_;
    model::SolveResult status_;
    model::ObjectId dragging_ = 0; // point being dragged in the Select tool
    std::vector<model::Vec2> pending_; // clicks of the shape being drawn
    std::optional<Snap> hover_;

    model::Vec2 center_;   // sketch point at the canvas centre
    double scale_ = 4;     // pixels per millimetre
    std::optional<truffle::gui::Point> pan_from_;
    model::Vec2 pan_center_;
};

} // namespace studio::cad
