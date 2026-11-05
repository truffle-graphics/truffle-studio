#include "studio/app.hpp"

#include "host/sdl_window.hpp"
#include "host/system_font.hpp"
#include "studio/check.hpp"
#include "studio/workspace.hpp"

#include "truffle/gui/painter.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace studio {
namespace gui = truffle::gui;
namespace renderer = truffle::renderer;
namespace rhi = truffle::rhi;
using Clock = gui::Context::Clock;

namespace {

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

// Presents one window's GUI through Truffle's Renderer on Metal.
class Presenter {
public:
    Presenter(rhi::Device& device, void* layer, rhi::Extent2D extent)
        : programs_(device), renderer_(device, &programs_),
          pacer_(need(renderer::FramePacer::create(device, 2))),
          painter_(need(gui::Painter::create(
              device, programs_, {.color_format = rhi::TextureFormat::bgra8_unorm}))) {
        surface_ = need(device.create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer, .handle = layer},
            .initialExtent = extent}));
        swapchain_ = need(device.create_swapchain(
            surface_, {.extent = extent, .format = rhi::TextureFormat::bgra8_unorm}));
    }
    ~Presenter() { static_cast<void>(pacer_.wait_idle()); }
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    gui::Painter& painter() noexcept { return painter_; }
    std::size_t frames() const noexcept { return frames_; }

    void draw(gui::Context& context, rhi::Extent2D extent) {
        if (swapchain_.desc().extent != extent) {
            need(pacer_.wait_idle());
            need(swapchain_.resize(extent));
        }
        const auto slot = need(pacer_.begin_frame());
        auto batches = need(painter_.record(context, slot, extent));
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{.colorAttachments = {{.clear = {0.05F, 0.05F, 0.06F, 1}}}},
            std::move(batches), true));
        const auto status = renderer_.render(graph, &swapchain_, pacer_.signal());
        pacer_.end_frame(renderer_.last_frame_stats().submitted);
        if (status.code == truffle::base::StatusCode::out_of_date) {
            need(pacer_.wait_idle());
            need(swapchain_.resize(extent));
            context.request_frame();
            return;
        }
        need(status);
        ++frames_;
    }

private:
    rhi::Surface surface_;
    rhi::Swapchain swapchain_;
    renderer::PipelineCache programs_;
    renderer::Renderer renderer_;
    renderer::FramePacer pacer_;
    gui::Painter painter_;
    std::size_t frames_ = 0;
};

void push_click(const SdlWindow& window, gui::Point at) {
    for (const bool down : {true, false}) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = window.id();
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = at.x;
        event.button.y = at.y;
        SDL_PushEvent(&event);
    }
}

void push_text(const SdlWindow& window, bool composing, const char* text) {
    SDL_Event event{};
    if (composing) {
        event.type = SDL_EVENT_TEXT_EDITING;
        event.edit.windowID = window.id();
        event.edit.text = text;
        event.edit.start = 1;
        event.edit.length = 0;
    } else {
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = window.id();
        event.text.text = text;
    }
    SDL_PushEvent(&event);
}

void push_shortcut(const SdlWindow& window, SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window.id();
    event.key.key = key;
    event.key.mod = mod;
    event.key.down = true;
    SDL_PushEvent(&event);
}

gui::Point center(gui::Context& context, gui::ElementId element) {
    const auto rect = need(context.absolute_bounds(element));
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

} // namespace

int run(const AppOptions& options) {
    SdlSession session(options.smoke);
    SdlWindow window("Truffle Studio", 1280, 800);
    auto instance = need(rhi::create_metal_instance());
    auto adapter = need(instance.adapter(0));
    auto device = need(adapter.request_device({.requiredFeatures = {rhi::Feature::presentation}}));
    const auto initial = window.metrics();
    if (!initial) { throw std::runtime_error("the Studio window has no drawable area"); }
    Presenter presenter(device, window.metal_layer(), initial->pixels);
    auto text = need(gui::TextEngine::create(device));
    const auto font = load_system_font(text, 13);
    const auto atlas = need(presenter.painter().add_texture(need(text.atlas_texture()).get()));
    model::Document document;
    Workspace workspace(document, text, font, atlas);
    auto& context = workspace.context;
    context.set_host(&window);

    bool running = true;
    const auto forward = [&](const gui::InputEvent& input) {
        const auto status = context.handle(input);
        if (!status.ok()) { std::cerr << "Input refused: " << status.message << '\n'; }
    };
    const auto dispatch = [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window.id())) {
            running = false;
            return;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) { context.request_frame(); }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window.id() &&
            event.key.key == SDLK_Z && (event.key.mod & SDL_KMOD_GUI) != 0 &&
            !workspace.text_editing()) {
            if ((event.key.mod & SDL_KMOD_SHIFT) != 0) {
                workspace.redo();
            } else {
                workspace.undo();
            }
            return;
        }
        window.translate(event, forward);
    };
    // One loop turn: sleep until input or the GUI's next timed update (bounded
    // by `cap` when given), then draw only if the GUI needs a frame.
    const auto iterate = [&](std::optional<std::chrono::milliseconds> cap) {
        const auto now = Clock::now();
        std::int32_t timeout = -1;
        if (context.needs_frame(now)) {
            timeout = 0;
        } else if (const auto due = context.next_update()) {
            const auto wait = std::chrono::ceil<std::chrono::milliseconds>(*due - now).count();
            timeout = static_cast<std::int32_t>(std::max<std::int64_t>(wait, 0));
        }
        if (cap && (timeout < 0 || timeout > cap->count())) {
            timeout = static_cast<std::int32_t>(cap->count());
        }
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, timeout)) {
            dispatch(event);
            while (SDL_PollEvent(&event)) { dispatch(event); }
        }
        workspace.update();
        const auto metrics = window.metrics();
        if (!metrics) { return; }
        need(context.set_viewport(metrics->logical, metrics->scale));
        if (context.needs_frame(Clock::now())) {
            need(context.prepare_frame(Clock::now()).status);
            presenter.draw(context, metrics->pixels);
        }
    };

    if (!options.smoke) {
        while (running) { iterate(std::nullopt); }
    } else {
        const auto settle = [&] {
            for (int i = 0; i < 20; ++i) { iterate(std::chrono::milliseconds(5)); }
        };
        settle();
        expect(presenter.frames() > 0, "the window presented no frame");
        const auto idle = presenter.frames();
        settle();
        expect(presenter.frames() == idle, "an idle GUI kept drawing");

        namespace dock = gui::dock;
        expect(dock::panel_ids(workspace.dock.layout()).size() == 3, "workspace panels are missing");

        push_click(window, center(context, workspace.visible.element()));
        settle();
        expect(!need(workspace.visible.checked()), "SDL pointer input did not toggle the checkbox");
        expect(need(workspace.status.text()) == "Cube hidden", "the status line did not update");
        expect(presenter.frames() > idle, "the property change was not drawn");

        push_click(window, center(context, workspace.name.element()));
        settle();
        expect(window.text_input_active(), "focusing the editor did not start text input");
        push_text(window, true, "\xE3\x81\x8B"); // か, still composing
        settle();
        expect(need(workspace.name.text()) == "Untitled", "composition changed committed text");
        push_text(window, false, "\xE6\x97\xA5\xE6\x9C\xAC"); // 日本
        settle();
        const auto edited = need(workspace.name.text());
        expect(edited.find("\xE6\x97\xA5\xE6\x9C\xAC") != std::string::npos,
               "committed IME text did not reach the editor");
        expect(edited.find("\xE3\x81\x8B") == std::string::npos, "preedit text was committed");
        expect(need(workspace.status.text()) == "Name: " + edited, "name edits were not reported");
        expect(document.state().name == edited, "the name edit did not reach the document");

        push_shortcut(window, SDLK_Z, SDL_KMOD_GUI);
        settle();
        expect(document.state().name == edited, "Cmd+Z undid the document while a text field was focused");
        push_click(window, center(context, workspace.undo_button.element()));
        settle();
        expect(document.state().name == "Untitled", "Undo did not revert the rename");
        expect(need(workspace.name.text()) == "Untitled", "the editor did not follow undo");
        push_shortcut(window, SDLK_Z, static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT));
        settle();
        expect(document.state().name == edited, "Shift+Cmd+Z did not redo the rename");
        expect(need(workspace.status.text()) == "Redo Rename", "redo was not reported");

        need(workspace.dock.dock("properties", "scene", dock::DropSide::center));
        settle();
        const auto moved = workspace.dock.save();
        expect(moved != workspace.default_layout, "docking a panel did not change the layout");
        need(workspace.dock.restore(workspace.default_layout));
        need(workspace.dock.restore(moved));
        expect(workspace.dock.save() == moved, "the layout did not round-trip");
        push_click(window, center(context, workspace.reset_layout.element()));
        settle();
        expect(workspace.dock.save() == workspace.default_layout, "Reset layout did not restore");
        expect(need(workspace.status.text()) == "Layout reset", "the reset was not reported");
        std::cout << "studio smoke: frames=" << presenter.frames() << " name=" << edited << '\n';
    }
    context.set_host(nullptr);
    return 0;
}

} // namespace studio
