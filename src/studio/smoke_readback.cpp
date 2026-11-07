#include "studio/smoke_readback.hpp"

#include "studio/check.hpp"
#include "truffle/gui/rendering.hpp"
#include "truffle/renderer/rhi_interop.hpp"
#include "truffle/rhi/rhi.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio {
namespace gui = truffle::gui;
namespace renderer = truffle::renderer;
namespace rhi = truffle::rhi;

void expect(bool value, const char* message) {
    if (!value) { throw std::runtime_error(std::string("smoke check failed: ") + message); }
}

void verify_clear_pixels(gui::Graphics& graphics,
                         gui::PixelSize pixels,
                         gui::Color clear) {
    auto& device = renderer::RhiInterop::native(gui::Rendering::device(graphics));
    const std::size_t row = static_cast<std::size_t>(pixels.width) * 4;
    std::vector<std::byte> bytes(row * pixels.height);
    auto texture = need(device.create_texture({
        .extent = {pixels.width, pixels.height, 1},
        .format = rhi::TextureFormat::bgra8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source}));
    auto buffer = need(device.create_buffer({
        .size = bytes.size(),
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback}));
    auto queue = need(device.queue(rhi::QueueKind::graphics));
    auto pool = need(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = need(pool.allocate());
    auto fence = need(device.create_fence());

    need(list.begin());
    need(list.barrier({.textures = {{
        .texture = &texture,
        .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::color_attachment,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::color_attachment_output,
        .destinationAccess = rhi::Access::color_attachment_write}}}));
    auto render = need(list.begin_rendering({
        .extent = {pixels.width, pixels.height},
        .colorAttachments = {{.texture = &texture,
                              .clear = {clear.r, clear.g, clear.b, clear.a}}}}));
    need(render.end());
    need(list.barrier({.textures = {{
        .texture = &texture,
        .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::transfer_source,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = rhi::Access::color_attachment_write,
        .destinationAccess = rhi::Access::transfer_read}}}));
    auto copy = need(list.begin_copy());
    need(copy.copy_texture_to_buffer(texture, buffer,
        {.layout = {.bytesPerRow = row},
         .texture = {.extent = texture.desc().extent}}));
    need(copy.end());
    need(list.end());
    std::array<rhi::CommandList*, 1> lists{&list};
    need(queue.submit(lists, &fence, 1));
    need(fence.wait(1, std::chrono::seconds(10)));
    need(buffer.read(0, bytes));

    constexpr std::array expected{std::byte{15}, std::byte{13},
                                  std::byte{13}, std::byte{255}};
    expect(!bytes.empty(), "the blank background has no native pixels");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        expect(bytes[i] == expected[i % expected.size()],
               "the native background is not uniformly opaque and blank");
    }
    std::cout << "studio blank pixels: " << pixels.width << 'x' << pixels.height
              << " BGRA=15,13,13,255\n";
}

} // namespace studio
