#include "studio/smoke_readback.hpp"

#include "studio/check.hpp"
#include "truffle/gui/rendering.hpp"
#include "truffle/renderer/rhi_interop.hpp"
#include "truffle/rhi/rhi.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace studio {
namespace gui = truffle::gui;
namespace renderer = truffle::renderer;
namespace rhi = truffle::rhi;

namespace {
void expect(bool value, const char* message) {
    if (!value) { throw std::runtime_error(std::string("smoke check failed: ") + message); }
}

struct OpenFrame {
    gui::Graphics& graphics;
    gui::Context& context;
    bool open = true;
    ~OpenFrame() {
        if (open) {
            const auto status = gui::Rendering::cancel_frame(graphics, context);
            if (!status.ok()) {
                std::cerr << "Could not cancel smoke frame: " << status.message << '\n';
            }
        }
    }
};

std::vector<std::byte> render_menu(gui::Graphics& graphics, gui::Context& context,
                                   gui::PixelSize pixels, gui::Color clear) {
    auto& device = gui::Rendering::device(graphics);
    auto target = need(renderer::SampledTexture::render_target(device, {.extent = pixels}));
    const auto frame = need(gui::Rendering::begin_frame(graphics, context));
    OpenFrame open{graphics, context};
    auto batches = need(gui::Rendering::record(graphics, context, frame.slot, frame.pixels));
    expect(!batches.empty(), "the menu bar produced no GUI batches");

    renderer::FrameGraph graph;
    renderer::RenderPassNode offscreen;
    offscreen.pass.extent = frame.pixels;
    offscreen.pass.colorAttachments.push_back({
        .texture = target.texture(), .clear = {clear.r, clear.g, clear.b, clear.a}});
    offscreen.batches = batches;
    offscreen.transitions.push_back(target.to_attachment());
    const auto target_pass = need(graph.add_render_pass(std::move(offscreen)));

    renderer::RenderPassNode onscreen;
    onscreen.pass.extent = frame.pixels;
    onscreen.pass.colorAttachments.push_back({.clear = {clear.r, clear.g, clear.b, clear.a}});
    onscreen.batches = std::move(batches);
    onscreen.useWindow = true;
    const auto window_pass = need(graph.add_render_pass(std::move(onscreen)));
    need(graph.add_dependency(target_pass, window_pass));
    const auto status = gui::Rendering::present(graphics, context, graph);
    open.open = false;
    if (!status.ok() && status.code != truffle::base::StatusCode::suboptimal) { need(status); }
    if (!status.ok()) { context.request_frame(); }
    expect(graphics.last_frame_stats().presented, "the menu frame was not presented");
    need(graphics.wait_idle());

    const auto texture = target.texture();
    auto* raw_texture = renderer::RhiInterop::native(texture);
    expect(raw_texture != nullptr, "the menu readback target is invalid");
    auto& raw_device = renderer::RhiInterop::native(device);
    const std::size_t row = static_cast<std::size_t>(pixels.width) * 4;
    std::vector<std::byte> bytes(row * pixels.height);
    auto buffer = need(raw_device.create_buffer({
        .size = bytes.size(),
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback}));
    auto queue = need(raw_device.queue(rhi::QueueKind::graphics));
    auto pool = need(raw_device.create_command_pool(rhi::QueueKind::graphics));
    auto list = need(pool.allocate());
    auto fence = need(raw_device.create_fence());

    need(list.begin());
    need(list.barrier({.textures = {{
        .texture = raw_texture,
        .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::transfer_source,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = rhi::Access::color_attachment_write,
        .destinationAccess = rhi::Access::transfer_read}}}));
    auto copy = need(list.begin_copy());
    need(copy.copy_texture_to_buffer(*raw_texture, buffer,
        {.layout = {.bytesPerRow = row}, .texture = {.extent = raw_texture->desc().extent}}));
    need(copy.end());
    need(list.end());
    std::array<rhi::CommandList*, 1> lists{&list};
    need(queue.submit(lists, &fence, 1));
    need(fence.wait(1, std::chrono::seconds(10)));
    need(buffer.read(0, bytes));
    return bytes;
}

using Pixel = std::array<unsigned, 4>;

Pixel pixel_at(const std::vector<std::byte>& bytes, gui::PixelSize pixels,
               gui::Point point, float scale) {
    const float x = std::floor(point.x * scale);
    const float y = std::floor(point.y * scale);
    expect(std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
               x < static_cast<float>(pixels.width) && y < static_cast<float>(pixels.height),
           "a menu pixel probe is outside the viewport");
    const auto offset = (static_cast<std::size_t>(y) * pixels.width +
                         static_cast<std::size_t>(x)) * 4;
    return {std::to_integer<unsigned>(bytes[offset]),
            std::to_integer<unsigned>(bytes[offset + 1]),
            std::to_integer<unsigned>(bytes[offset + 2]),
            std::to_integer<unsigned>(bytes[offset + 3])};
}

std::size_t bright_pixels(const std::vector<std::byte>& bytes, gui::PixelSize pixels,
                          gui::Rect rect, float scale) {
    expect(std::isfinite(scale) && scale > 0 && rect.x >= 0 && rect.y >= 0 &&
               rect.width > 0 && rect.height > 0 &&
               (rect.x + rect.width) * scale <= static_cast<float>(pixels.width) + 1 &&
               (rect.y + rect.height) * scale <= static_cast<float>(pixels.height) + 1,
           "a menu text region is outside the viewport");
    const auto left = static_cast<std::uint32_t>(std::floor(rect.x * scale));
    const auto top = static_cast<std::uint32_t>(std::floor(rect.y * scale));
    const auto right = std::min(pixels.width,
        static_cast<std::uint32_t>(std::ceil((rect.x + rect.width) * scale)));
    const auto bottom = std::min(pixels.height,
        static_cast<std::uint32_t>(std::ceil((rect.y + rect.height) * scale)));
    std::size_t count = 0;
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
            if (std::to_integer<unsigned>(bytes[offset]) > 170 &&
                std::to_integer<unsigned>(bytes[offset + 1]) > 170 &&
                std::to_integer<unsigned>(bytes[offset + 2]) > 170) {
                ++count;
            }
        }
    }
    return count;
}
} // namespace

void verify_menu_pixels(gui::Graphics& graphics, gui::Context& context,
                        gui::PixelSize pixels, gui::Color clear,
                        const MenuReadbackRegions& regions) {
    const auto bytes = render_menu(graphics, context, pixels, clear);
    constexpr Pixel background{15, 13, 13, 255};
    const auto bar = pixel_at(bytes, pixels,
        {regions.bar.x + regions.bar.width - 6, regions.bar.y + regions.bar.height * 0.5F},
        regions.scale);
    expect(bar != background && bar[3] == 255, "the menu bar is not visible");
    const auto empty = pixel_at(bytes, pixels,
        {regions.bar.x + regions.bar.width - 6,
         static_cast<float>(pixels.height) / regions.scale - 6},
        regions.scale);
    expect(empty == background, "the client outside the menu is not the expected background");
    expect(bright_pixels(bytes, pixels, regions.file_trigger, regions.scale) >= 4,
           "the File label has no visible text pixels");
    expect(bright_pixels(bytes, pixels, regions.edit_trigger, regions.scale) >= 4,
           "the Edit label has no visible text pixels");
    if (regions.popup) {
        const auto popup = *regions.popup;
        const auto sample = pixel_at(bytes, pixels,
            {popup.x + popup.width - 6, popup.y + popup.height * 0.5F}, regions.scale);
        expect(sample != background && sample[3] == 255, "the open menu popup is not visible");
        expect(regions.popup_item.has_value() &&
                   bright_pixels(bytes, pixels, *regions.popup_item, regions.scale) >= 4,
               "the open menu command has no visible text pixels");
    } else {
        const auto below = pixel_at(bytes, pixels,
            {regions.file_trigger.x + 6, regions.bar.y + regions.bar.height + 6}, regions.scale);
        expect(below == background, "a closed menu left visible popup pixels");
    }
    std::cout << "studio menu pixels: " << pixels.width << 'x' << pixels.height
              << (regions.popup ? " open" : " closed") << '\n';
}

} // namespace studio
