#pragma once

#include "studio/workspace.hpp"

#include <cstdint>

namespace studio::cad {

// Sketch-based parametric part modelling.
class CadWorkspace final : public Workspace {
public:
    std::string_view id() const noexcept override { return "cad"; }
    std::string_view title() const noexcept override { return "CAD"; }
    EngineProfile profile() const override;
    void build(WorkspaceServices& services, truffle::gui::dock::DockSpace& dock) override;
    void teardown() override;
    bool text_editing(const truffle::gui::Context& context) const override;

    truffle::gui::Widget part_name;

private:
    void sync();

    WorkspaceServices* services_ = nullptr;
    std::uint64_t subscription_ = 0;
};

} // namespace studio::cad
