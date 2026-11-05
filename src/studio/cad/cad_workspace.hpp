#pragma once

#include "studio/cad/model_view.hpp"
#include "studio/cad/sketch_editor.hpp"
#include "studio/workspace.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace studio::cad {

// Sketch-based parametric part modelling.
class CadWorkspace final : public Workspace {
public:
    std::string_view id() const noexcept override { return "cad"; }
    std::string_view title() const noexcept override { return "CAD"; }
    EngineProfile profile() const override;
    void build(WorkspaceServices& services, truffle::gui::dock::DockSpace& dock) override;
    void teardown() override;
    void update() override;
    void before_frame() override;
    bool text_editing(const truffle::gui::Context& context) const override;

    // Switches the viewport between the 2D sketch editor and the 3D view.
    void show_model(bool model);
    bool showing_model() const noexcept { return view.visible(); }

    SketchEditor editor;
    ModelView view;
    truffle::gui::Widget sketch_mode;
    truffle::gui::Widget model_mode;
    truffle::gui::Widget fit_view;
    truffle::gui::Widget part_name;
    truffle::gui::Widget sketch_info;
    truffle::gui::Widget new_sketch;
    // One button per sketch, in document order.
    std::vector<truffle::gui::Widget> sketch_buttons;

private:
    struct Entry {
        truffle::gui::ElementId highlight;
        model::ObjectId sketch = 0;
    };

    void sync();
    void rebuild_sketch_list();
    void refresh();
    std::string sketch_signature() const;

    WorkspaceServices* services_ = nullptr;
    std::uint64_t subscription_ = 0;
    truffle::gui::ElementId sketch_list_;
    truffle::gui::ElementId sketch_page_;
    truffle::gui::ElementId model_page_;
    truffle::gui::ElementId sketch_highlight_;
    truffle::gui::ElementId model_highlight_;
    std::vector<Entry> entries_;
    std::string listed_;
};

} // namespace studio::cad
