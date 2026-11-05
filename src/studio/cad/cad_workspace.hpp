#pragma once

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
    bool text_editing(const truffle::gui::Context& context) const override;

    SketchEditor editor;
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
    std::vector<Entry> entries_;
    std::string listed_;
};

} // namespace studio::cad
