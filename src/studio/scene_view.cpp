#include "studio/scene_view.hpp"

#include "studio/check.hpp"

#include "truffle/renderer/source_program.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace studio {
namespace gui = truffle::gui;
namespace renderer = truffle::renderer;
namespace rhi = truffle::rhi;
namespace shader = truffle::shader;

namespace {
constexpr renderer::MaterialId scene_material = 0x7374'7564'696f'0001ULL; // "studio" 1
constexpr auto color_format = rhi::TextureFormat::bgra8_unorm;

struct Uniforms {
    std::array<float, 16> view_projection;
    std::array<float, 4> light;  // xyz towards the light, w ambient
    std::array<float, 4> albedo;
};
static_assert(sizeof(Uniforms) == 96);

struct Vertex {
    float position[3];
    float normal[3];
};
static_assert(sizeof(Vertex) == 24);

constexpr const char* shader_source = R"(#include <metal_stdlib>
using namespace metal;

struct SceneInput {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
};
struct SceneOutput {
    float4 position [[position]];
    float3 normal [[user(locn0)]];
};
struct SceneUniforms {
    float4x4 viewProjection;
    float4 light;
    float4 albedo;
};

vertex SceneOutput scene_vertex(SceneInput input [[stage_in]],
                                constant SceneUniforms& uniforms [[buffer(16)]]) {
    SceneOutput result;
    result.position = uniforms.viewProjection * float4(input.position, 1.0);
    result.normal = input.normal;
    return result;
}

fragment float4 scene_fragment(SceneOutput input [[stage_in]],
                               constant SceneUniforms& uniforms [[buffer(16)]]) {
    float3 n = normalize(input.normal);
    float diffuse = abs(dot(n, uniforms.light.xyz));
    float shade = uniforms.light.w + (1.0 - uniforms.light.w) * diffuse;
    return float4(uniforms.albedo.rgb * shade, 1.0);
}
)";

shader::CompileRequest compile_request(rhi::ShaderStage stage) {
    shader::CompileRequest result;
    result.name = "Studio scene shader";
    result.source = {"scene.metal", shader_source};
    result.language = rhi::ShaderSourceLanguage::msl;
    result.target = rhi::ShaderTarget::metal_source;
    result.stage = stage;
    rhi::ShaderReflection reflection;
    const auto add = [&](rhi::ShaderInterfaceDirection direction, std::uint32_t location,
                         const char* name, const char* type) {
        reflection.interfaces.push_back({stage, direction, location, name, type, "smooth"});
    };
    if (stage == rhi::ShaderStage::vertex) {
        result.entryPoint = "scene_vertex";
        add(rhi::ShaderInterfaceDirection::input, 0, "position", "vec3");
        add(rhi::ShaderInterfaceDirection::input, 1, "normal", "vec3");
        add(rhi::ShaderInterfaceDirection::output, 0, "normal", "vec3");
    } else {
        result.entryPoint = "scene_fragment";
        add(rhi::ShaderInterfaceDirection::input, 0, "normal", "vec3");
        add(rhi::ShaderInterfaceDirection::output, 0, "color", "vec4");
    }
    reflection.bindings = {{.name = "uniforms", .stage = stage, .group = 0, .binding = 0,
                            .minimumSize = sizeof(Uniforms)}};
    result.interfaceDeclaration = std::move(reflection);
    return result;
}
} // namespace

SceneView::SceneView(rhi::Device& device, renderer::PipelineCache& programs, gui::Painter& painter,
                     renderer::FramePacer& pacer)
    : device_(&device), painter_(&painter), pacer_(&pacer), depth_(device) {
    bindings_ = need(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::vertex | rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = sizeof(Uniforms)}},
    }));
    layout_ = need(device.create_pipeline_layout({.bindGroupLayouts = {&bindings_}}));
    arena_ = need(device.create_descriptor_arena());
    for (auto& slot : slots_) {
        slot.uniforms = need(device.create_buffer({
            .size = sizeof(Uniforms), .usage = rhi::BufferUsage::uniform,
            .memory = rhi::MemoryDomain::upload, .debugName = "Studio scene uniforms",
        }));
        slot.group = need(device.create_bind_group({
            .layout = &bindings_, .arena = &arena_,
            .entries = {{.binding = 0, .buffer = &slot.uniforms, .size = sizeof(Uniforms)}},
        }));
    }
    rhi::PipelineDesc state;
    state.layout = &layout_;
    state.vertexBuffers = {{
        .stride = sizeof(Vertex),
        .attributes = {{0, rhi::VertexFormat::float32x3, offsetof(Vertex, position)},
                       {1, rhi::VertexFormat::float32x3, offsetof(Vertex, normal)}},
    }};
    state.colorTargets = {{.format = color_format}};
    state.depthStencil = {.format = rhi::TextureFormat::depth32_float, .depthWriteEnabled = true,
                          .depthCompare = rhi::CompareOp::less};
    shader::Compiler compiler;
    renderer::SourceProgram program(programs, scene_material, state);
    need(program.rebuild(compiler, {.vertex = compile_request(rhi::ShaderStage::vertex),
                                    .fragment = compile_request(rhi::ShaderStage::fragment)}));
}

SceneView::~SceneView() {
    static_cast<void>(pacer_->wait_idle());
    if (texture_.valid()) { static_cast<void>(painter_->remove_texture(texture_)); }
}

void SceneView::set_mesh(const kernel::Mesh& mesh) {
    need(pacer_->wait_idle()); // the old mesh may still be drawing
    mesh_.reset();
    const auto count = mesh.positions.size() / 3;
    if (mesh.indices.empty() || count == 0) { return; }
    if (count > std::numeric_limits<std::uint32_t>::max() || mesh.normals.size() != mesh.positions.size()) {
        throw std::runtime_error("SceneView: malformed mesh");
    }
    std::vector<Vertex> vertices(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            vertices[i].position[axis] = mesh.positions[i * 3 + axis];
            vertices[i].normal[axis] = mesh.normals[i * 3 + axis];
        }
    }
    const std::array streams{renderer::MeshStream{std::as_bytes(std::span{vertices}), sizeof(Vertex)}};
    mesh_ = need(renderer::Mesh::upload(*device_, {
        .streams = streams, .vertexCount = static_cast<std::uint32_t>(count),
        .indices = std::as_bytes(std::span{mesh.indices}),
    }));
}

void SceneView::resize(rhi::Extent2D extent) {
    need(pacer_->wait_idle()); // frames in flight may sample the old target
    if (texture_.valid()) { need(painter_->remove_texture(texture_)); }
    texture_ = {};
    target_.reset();
    target_ = need(renderer::SampledTexture::render_target(
        *device_, {.extent = extent, .format = color_format, .debugName = "Studio scene"}));
    need(depth_.resize(extent));
    texture_ = need(painter_->add_texture(*target_));
}

gui::TextureId SceneView::request(const SceneFrame& frame) {
    const rhi::Extent2D extent{std::max(frame.extent.width, 1U), std::max(frame.extent.height, 1U)};
    if (!target_ || target_->texture().desc().extent.width != extent.width ||
        target_->texture().desc().extent.height != extent.height) {
        resize(extent);
    }
    requested_ = frame;
    requested_->extent = extent;
    return texture_;
}

std::optional<renderer::FrameGraph::NodeId> SceneView::add_pass(renderer::FrameGraph& graph,
                                                                std::uint32_t slot) {
    if (!requested_ || !target_) { return std::nullopt; }
    const auto frame = *requested_;
    requested_.reset();
    auto& uniforms = slots_[slot % slots_.size()];
    const Uniforms values{frame.view_projection, {frame.light[0], frame.light[1], frame.light[2], 0.3F},
                          {0.72F, 0.75F, 0.8F, 1}};
    need(uniforms.uniforms.write(0, std::as_bytes(std::span{&values, 1})));
    std::vector<renderer::RenderBatch> batches;
    if (mesh_) {
        auto batch = mesh_->batch(scene_material);
        batch.bindGroups = {{.group = 0, .bindings = &uniforms.group}};
        batches.push_back(std::move(batch));
    }
    auto node = std::make_unique<renderer::RenderPassNode>(
        rhi::RenderPassDesc{
            .extent = frame.extent,
            .colorAttachments = {{.texture = &target_->texture(),
                                  .clear = {frame.clear[0], frame.clear[1], frame.clear[2], frame.clear[3]}}},
            .depthStencilAttachment = {.texture = depth_.texture()},
        },
        std::move(batches));
    node->transition_before(target_->to_attachment());
    ++passes_;
    return graph.add_node(std::move(node));
}

} // namespace studio
