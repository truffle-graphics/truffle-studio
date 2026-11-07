#include "studio/app.hpp"

#include "host/sdl_window.hpp"
#include "model/document.hpp"
#include "studio/check.hpp"

#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/renderer.hpp"
#if defined(_WIN32)
#include "truffle/rhi/vulkan_backend.hpp"
#else
#include "truffle/rhi/metal_backend.hpp"
#endif

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace studio {
namespace renderer = truffle::renderer;
namespace rhi = truffle::rhi;

namespace {
constexpr rhi::ClearColor background{13.0F / 255, 13.0F / 255, 15.0F / 255, 1};
constexpr std::array background_bgra{std::byte{15}, std::byte{13}, std::byte{13}, std::byte{255}};
#if defined(_WIN32)
constexpr SDL_Keymod shortcut_modifier = SDL_KMOD_CTRL;
#else
constexpr SDL_Keymod shortcut_modifier = SDL_KMOD_GUI;
#endif

void expect(bool value, const char* message) {
    if (!value) { throw std::runtime_error(std::string("smoke check failed: ") + message); }
}

class SdlSession {
public:
    explicit SdlSession(bool smoke) {
        // A smoke run must not take focus or a Dock icon from the developer.
        if (smoke) { SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); }
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
        }
    }
    ~SdlSession() { SDL_Quit(); }
    SdlSession(const SdlSession&) = delete;
    SdlSession& operator=(const SdlSession&) = delete;
};

// The client area is only a clear pass; legacy UI is never constructed.
class Presenter {
public:
    Presenter(rhi::Device& device, rhi::NativeSurface native, rhi::Extent2D extent)
        : device_(&device), renderer_(device),
          pacer_(need(renderer::FramePacer::create(device, 2))) {
        surface_ = need(device.create_surface({
            .native = native,
            .initialExtent = extent}));
        swapchain_ = need(device.create_swapchain(
            surface_, {.extent = extent, .format = rhi::TextureFormat::bgra8_unorm}));
    }
    ~Presenter() { static_cast<void>(pacer_.wait_idle()); }
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    std::size_t frames() const noexcept { return frames_; }

    bool draw(rhi::Extent2D extent) {
        if (swapchain_.desc().extent != extent) {
            need(pacer_.wait_idle());
            need(swapchain_.resize(extent));
        }
        static_cast<void>(need(pacer_.begin_frame()));
        renderer::FrameGraph graph;
        graph.add_node(clear_pass());
        const auto status = renderer_.render(graph, &swapchain_, pacer_.signal());
        pacer_.end_frame(renderer_.last_frame_stats().submitted);
        if (status.code == truffle::base::StatusCode::out_of_date) {
            need(pacer_.wait_idle());
            need(swapchain_.resize(extent));
            return false;
        }
        need(status);
        ++frames_;
        return true;
    }

    void check_blank_for_smoke() {
        need(pacer_.wait_idle());
        const auto& stats = renderer_.last_frame_stats();
        expect(stats.presented && stats.renderNodesExecuted == 1 &&
                   stats.renderBatchesExecuted == 0 && stats.computeNodesExecuted == 0,
               "the client frame was not a single clear-only presentation");

        const auto extent = swapchain_.desc().extent;
        auto texture = need(device_->create_texture({
            .extent = {extent.width, extent.height, 1},
            .format = rhi::TextureFormat::bgra8_unorm,
            .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source}));
        renderer::FrameGraph graph;
        // Read the same pass used for presentation, without swapchain readback assumptions.
        graph.add_node(clear_pass(&texture));
        need(renderer_.render(graph));
        const std::size_t row = static_cast<std::size_t>(extent.width) * 4;
        std::vector<std::byte> pixels(row * extent.height);
        auto buffer = need(device_->create_buffer({
            .size = pixels.size(), .usage = rhi::BufferUsage::copy_destination,
            .memory = rhi::MemoryDomain::readback}));
        auto queue = need(device_->queue(rhi::QueueKind::graphics));
        auto pool = need(device_->create_command_pool(rhi::QueueKind::graphics));
        auto list = need(pool.allocate());
        auto fence = need(device_->create_fence());
        need(list.begin());
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
            {.layout = {.bytesPerRow = row}, .texture = {.extent = texture.desc().extent}}));
        need(copy.end());
        need(list.end());
        std::array<rhi::CommandList*, 1> lists{&list};
        need(queue.submit(lists, &fence, 1));
        need(fence.wait(1, std::chrono::seconds(10)));
        need(buffer.read(0, pixels));
        expect(!pixels.empty(), "the blank background has no native pixels");
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            expect(pixels[i] == background_bgra[i % 4],
                   "the native background is not uniformly opaque and blank");
        }
        std::cout << "studio blank pixels: " << extent.width << 'x' << extent.height
                  << " BGRA=15,13,13,255\n";
    }

private:
    static std::unique_ptr<renderer::RenderPassNode> clear_pass(rhi::Texture* target = nullptr) {
        rhi::RenderPassDesc desc{
            .colorAttachments = {{.texture = target, .clear = background}}};
        if (target) {
            desc.extent = {target->desc().extent.width, target->desc().extent.height};
        }
        auto pass = std::make_unique<renderer::RenderPassNode>(
            std::move(desc), std::vector<renderer::RenderBatch>{}, target == nullptr);
        if (target) {
            pass->transition_before({
                .texture = target,
                .oldLayout = rhi::TextureLayout::undefined,
                .newLayout = rhi::TextureLayout::color_attachment,
                .sourceStages = rhi::PipelineStage::top,
                .destinationStages = rhi::PipelineStage::color_attachment_output,
                .destinationAccess = rhi::Access::color_attachment_write});
        }
        return pass;
    }

    rhi::Device* device_;
    rhi::Surface surface_;
    rhi::Swapchain swapchain_;
    renderer::Renderer renderer_;
    renderer::FramePacer pacer_;
    std::size_t frames_ = 0;
};

void push_shortcut(const SdlWindow& window, SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window.id();
    event.key.key = key;
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    event.key.mod = mod;
    event.key.down = true;
    expect(SDL_PushEvent(&event), "could not queue a keyboard event");
}

} // namespace

int run(const AppOptions& options) {
    std::atomic_size_t diagnostics{0};
    SdlSession session(options.smoke);
    SdlWindow window("Truffle Studio", 1280, 800);
#if defined(_WIN32)
    auto instance = need(rhi::create_vulkan_instance({
        .enableValidation = options.smoke,
        .debugCallback = [](const rhi::BackendDiagnostic& detail, void* user) {
            static_cast<std::atomic_size_t*>(user)->fetch_add(1);
            std::cerr << "Studio graphics diagnostic: " << detail.message << '\n';
        },
        .debugUserData = &diagnostics}));
#else
    auto instance = need(rhi::create_metal_instance());
#endif
    auto adapter = need(instance.adapter(0));
    if (options.smoke) { std::cout << "Studio native adapter: " << adapter.info().name << '\n'; }
    auto device = need(adapter.request_device({.requiredFeatures = {rhi::Feature::presentation}}));
    const auto initial = window.metrics();
    if (!initial) { throw std::runtime_error("the Studio window has no drawable area"); }
    Presenter presenter(device, window.surface_handle(), initial->pixels);
    model::Document document;

    bool running = true;
    bool redraw = true;
    const auto dispatch = [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window.id())) {
            running = false;
            return;
        }
        if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             event.type == SDL_EVENT_WINDOW_EXPOSED ||
             event.type == SDL_EVENT_WINDOW_RESTORED ||
             event.type == SDL_EVENT_WINDOW_SHOWN) && event.window.windowID == window.id()) {
            redraw = true;
        }
    };
    const auto iterate = [&](std::optional<std::chrono::milliseconds> cap) {
        const bool drawable = window.metrics().has_value();
        const std::int32_t timeout = drawable && redraw ? 0
            : cap ? static_cast<std::int32_t>(cap->count()) : -1;
        SDL_Event event{};
        if (SDL_WaitEventTimeout(&event, timeout)) {
            dispatch(event);
            while (SDL_PollEvent(&event)) { dispatch(event); }
        }
        if (!running) { return; }
        const auto metrics = window.metrics();
        if (metrics && redraw) { redraw = !presenter.draw(metrics->pixels); }
    };

    if (!options.smoke) {
        while (running) { iterate(std::nullopt); }
    } else {
        const auto settle = [&] {
            for (int i = 0; i < 20; ++i) { iterate(std::chrono::milliseconds(5)); }
        };
        const auto check_blank = [&] {
            redraw = true;
            settle();
            presenter.check_blank_for_smoke();
            expect(!window.text_input_active(), "the blank window started a text editor");
        };
        settle();
        expect(presenter.frames() > 0, "the window presented no frame");
        const auto idle = presenter.frames();
        settle();
        expect(presenter.frames() == idle, "an idle blank window kept drawing");
        check_blank();

#if defined(_WIN32)
        expect(SDL_MinimizeWindow(window.native()), "could not minimize the smoke window");
        settle();
        expect(!window.metrics(), "a minimized window remained drawable");
        const auto minimized = presenter.frames();
        redraw = true;
        settle();
        expect(presenter.frames() == minimized, "a minimized blank window kept drawing");
        expect(SDL_RestoreWindow(window.native()), "could not restore the smoke window");
        settle();
        expect(window.metrics().has_value() && presenter.frames() > minimized,
               "restoring the window did not resume presentation");
        check_blank();
#endif
        for (const auto size : {rhi::Extent2D{1024, 700}, rhi::Extent2D{1600, 900},
                                rhi::Extent2D{1280, 800}}) {
            const auto before = presenter.frames();
            expect(SDL_SetWindowSize(window.native(), static_cast<int>(size.width),
                                     static_cast<int>(size.height)),
                   "could not resize the smoke window");
            settle();
            expect(presenter.frames() > before, "resizing did not redraw the blank window");
            check_blank();
        }

        expect(document.edit("Smoke document", [](model::DocumentState& state) {
            state.name = "Preserved document";
        }), "could not prepare document history");
        const auto saved = document.state();
        const auto revision = document.revision();
        for (const auto point : {std::array{24.0F, 24.0F}, std::array{1250.0F, 24.0F},
                                 std::array{24.0F, 760.0F}, std::array{1250.0F, 760.0F},
                                 std::array{640.0F, 400.0F}}) {
            for (const auto button : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE}) {
                for (const bool down : {true, false}) {
                    SDL_Event event{};
                    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    event.button.windowID = window.id();
                    event.button.button = static_cast<std::uint8_t>(button);
                    event.button.down = down;
                    event.button.clicks = 1;
                    event.button.x = point[0];
                    event.button.y = point[1];
                    expect(SDL_PushEvent(&event), "could not queue a pointer event");
                }
            }
        }
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = window.id();
        wheel.wheel.y = 2;
        wheel.wheel.mouse_x = 640;
        wheel.wheel.mouse_y = 400;
        expect(SDL_PushEvent(&wheel), "could not queue a wheel event");
        for (const auto key : {SDLK_S, SDLK_L, SDLK_R, SDLK_C, SDLK_A,
                               SDLK_BACKSPACE, SDLK_DELETE, SDLK_ESCAPE, SDLK_RETURN, SDLK_TAB}) {
            push_shortcut(window, key, SDL_KMOD_NONE);
        }
        push_shortcut(window, SDLK_Z, shortcut_modifier);
        push_shortcut(window, SDLK_Z, static_cast<SDL_Keymod>(shortcut_modifier | SDL_KMOD_SHIFT));
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = window.id();
        motion.motion.x = 700;
        motion.motion.y = 420;
        motion.motion.state = SDL_BUTTON_LMASK;
        expect(SDL_PushEvent(&motion), "could not queue a drag motion event");
        SDL_Event composition{};
        composition.type = SDL_EVENT_TEXT_EDITING;
        composition.edit.windowID = window.id();
        composition.edit.text = "No editor";
        expect(SDL_PushEvent(&composition), "could not queue a composition event");
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.windowID = window.id();
        text.text.text = "No editor";
        expect(SDL_PushEvent(&text), "could not queue a text event");
        const auto before_input = presenter.frames();
        settle();
        expect(document.state() == saved && document.revision() == revision &&
                   document.can_undo() && !document.can_redo(),
               "legacy input changed the preserved document");
        expect(presenter.frames() == before_input, "inert client input requested a frame");
        check_blank();

        SDL_Event close{};
        close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = window.id() + 1;
        expect(SDL_PushEvent(&close), "could not queue another window's close event");
        settle();
        expect(running, "another window's close event closed Studio");
        close.window.windowID = window.id();
        expect(SDL_PushEvent(&close), "could not queue the Studio close event");
        settle();
        expect(!running, "the native close event did not close Studio");
        expect(diagnostics.load() == 0, "native rendering emitted diagnostics");
        std::cout << "studio blank smoke: frames=" << presenter.frames()
                  << " no UI batches; input inert; lifecycle passed\n";
    }
    return 0;
}

} // namespace studio
