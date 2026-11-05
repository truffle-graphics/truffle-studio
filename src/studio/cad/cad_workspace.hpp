#pragma once

#include "features/regenerate.hpp"
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
    truffle::gui::Widget extrude_button;
    // The selected extrude's distance; only alive while one is selected.
    truffle::gui::Widget extrude_distance;
    // The feature tree: one button per sketch and per extrude, in document order.
    std::vector<truffle::gui::Widget> sketch_buttons;
    std::vector<truffle::gui::Widget> extrude_buttons;

    const features::Regeneration& regeneration() const noexcept { return regeneration_; }
    model::ObjectId selected_extrude() const noexcept { return selected_extrude_; }

private:
    struct Entry {
        truffle::gui::ElementId highlight;
        model::ObjectId sketch = 0;
        model::ObjectId extrude = 0;
    };

    void sync();
    void rebuild_feature_list();
    void refresh();
    void regenerate();
    void update_distance_field();
    void submit_distance();
    void extrude_active_sketch();
    std::string feature_signature() const;

    WorkspaceServices* services_ = nullptr;
    std::uint64_t subscription_ = 0;
    truffle::gui::ElementId feature_list_;
    truffle::gui::ElementId inspector_;
    truffle::gui::ElementId distance_row_;
    truffle::gui::ElementId sketch_page_;
    truffle::gui::ElementId model_page_;
    truffle::gui::ElementId sketch_highlight_;
    truffle::gui::ElementId model_highlight_;
    std::vector<Entry> entries_;
    std::string listed_;
    model::ObjectId selected_extrude_ = 0;
    model::ObjectId distance_for_ = 0;
    bool fit_pending_ = false;
    // The inputs of the last regeneration; it reruns only when they change.
    std::vector<model::Extrude> built_extrudes_;
    std::vector<model::Sketch> built_sketches_;
    bool built_ = false;
    features::Regeneration regeneration_;
    std::string reported_errors_;
};

} // namespace studio::cad
