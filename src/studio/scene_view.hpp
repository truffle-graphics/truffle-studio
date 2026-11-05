#pragma once

#include "kernel/kernel.hpp"

#include "truffle/gui/painter.hpp"
#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/frame_graph.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/texture.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace studio {

// What one 3D pass draws.
struct SceneFrame {
    truffle::rhi::Extent2D extent;        // target size in pixels
    std::array<float, 16> view_projection; // column-major
    std::array<float, 3> light{0, 0, 1};  // unit direction towards the light
    std::array<float, 4> clear{0.11F, 0.12F, 0.14F, 1};
};

// Renders shaded solids into an offscreen color target with depth, which the
// GUI shows as a texture. A workspace requests a pass once per frame; the
// presenter adds it to the frame graph before the GUI pass.
class SceneView {
public:
    SceneView(truffle::rhi::Device& device, truffle::renderer::PipelineCache& programs,
              truffle::gui::Painter& painter, truffle::renderer::FramePacer& pacer);
    ~SceneView();
    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;

    // Replaces the shown triangles (model units); an empty mesh clears them.
    void set_mesh(const kernel::Mesh& mesh);
    bool has_mesh() const noexcept { return mesh_.has_value(); }

    // Requests a pass this frame and returns the GUI texture that shows it.
    // The texture changes when the extent does.
    truffle::gui::TextureId request(const SceneFrame& frame);

    // Presenter only: adds the requested pass, if any, and clears the request.
    std::optional<truffle::renderer::FrameGraph::NodeId> add_pass(
        truffle::renderer::FrameGraph& graph, std::uint32_t slot);
    // The barrier the pass sampling the target needs after add_pass.
    truffle::rhi::TextureBarrier to_shader_read() { return target_->to_shader_read(); }
    std::size_t passes() const noexcept { return passes_; }

private:
    struct Slot {
        truffle::rhi::Buffer uniforms;
        truffle::rhi::BindGroup group;
    };

    void resize(truffle::rhi::Extent2D extent);

    truffle::rhi::Device* device_;
    truffle::gui::Painter* painter_;
    truffle::renderer::FramePacer* pacer_;
    truffle::rhi::BindGroupLayout bindings_;
    truffle::rhi::PipelineLayout layout_;
    truffle::rhi::DescriptorArena arena_;
    std::array<Slot, 2> slots_;
    std::optional<truffle::renderer::SampledTexture> target_;
    truffle::renderer::DepthTarget depth_;
    truffle::gui::TextureId texture_;
    std::optional<truffle::renderer::Mesh> mesh_;
    std::optional<SceneFrame> requested_;
    std::size_t passes_ = 0;
};

} // namespace studio
