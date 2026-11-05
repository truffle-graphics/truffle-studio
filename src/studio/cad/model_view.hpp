#pragma once

#include "kernel/kernel.hpp"
#include "model/document.hpp"
#include "studio/cad/orbit_camera.hpp"
#include "studio/workspace.hpp"

#include <optional>

namespace studio::cad {

// The 3D view of the part: shaded solids rendered by the SceneView, with the
// sketches and axes drawn over them. Left drag orbits, right or middle drag
// pans and the wheel zooms.
class ModelView {
public:
    void build(WorkspaceServices& services, truffle::gui::ElementId parent);
    void teardown();

    // Replaces the shown solid; an empty mesh clears it.
    void set_mesh(const kernel::Mesh& mesh);
    // Frames the solid and every sketch.
    void fit();
    // Hidden views request no 3D passes.
    void set_visible(bool visible);
    bool visible() const noexcept { return visible_; }
    void set_active_sketch(model::ObjectId sketch);
    // Requests this frame's 3D pass; the workspace calls it from before_frame.
    void before_frame();
    void redraw();

    truffle::gui::ElementId element() const noexcept { return view_; }
    OrbitCamera camera;

private:
    void handle(truffle::gui::Context& context, truffle::gui::RoutedEvent& event);
    void paint(truffle::gui::ShapeWriter& out, truffle::gui::Size size) const;
    void end_drag(truffle::gui::Context& context);

    enum class Drag : std::uint8_t { none, orbit, pan };

    WorkspaceServices* services_ = nullptr;
    truffle::gui::ElementId view_;
    truffle::gui::TextureId shown_;
    std::optional<kernel::Bounds> mesh_bounds_;
    model::ObjectId active_ = 0;
    bool visible_ = false;
    Drag drag_ = Drag::none;
    std::uint32_t pointer_ = 0;
    truffle::gui::Point last_;
};

} // namespace studio::cad
