#include "studio/app.hpp"

#include "host/sdl_window.hpp"
#include "host/system_font.hpp"
#include "studio/check.hpp"
#include "model/sketch_edit.hpp"
#include "studio/cad/cad_workspace.hpp"
#include "studio/shell.hpp"

#include "truffle/gui/painter.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <SDL3/SDL.h>

#include <array>
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
    void set_clear(std::array<float, 4> clear) noexcept { clear_ = clear; }
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
            rhi::RenderPassDesc{.colorAttachments = {{.clear = {clear_[0], clear_[1], clear_[2], clear_[3]}}}},
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
    std::array<float, 4> clear_{0.05F, 0.05F, 0.06F, 1};
};

// A second workspace used only by the smoke test to exercise switching.
class ProbeWorkspace final : public Workspace {
public:
    std::string_view id() const noexcept override { return "probe"; }
    std::string_view title() const noexcept override { return "Probe"; }
    EngineProfile profile() const override {
        return {.name = "Probe", .clear = {0.2F, 0.1F, 0.1F, 1}, .continuous = true};
    }
    void build(WorkspaceServices& services, gui::dock::DockSpace& space) override {
        need(space.add_panel("notes", "Notes", services.ui.column(8)));
        need(space.set_layout(gui::dock::DockNode::tabs({"notes"})));
    }
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
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
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
    Shell shell(document, text, font, atlas);
    shell.add(std::make_unique<cad::CadWorkspace>());
    shell.add(std::make_unique<PlannedWorkspace>("animation", "Animation"));
    shell.add(std::make_unique<PlannedWorkspace>("simulation", "Simulation"));
    shell.add(std::make_unique<PlannedWorkspace>("design", "Design"));
    if (options.smoke) { shell.add(std::make_unique<ProbeWorkspace>()); }
    shell.activate("cad");
    auto& context = shell.context;
    std::string profile;
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
            !shell.text_editing()) {
            if ((event.key.mod & SDL_KMOD_SHIFT) != 0) {
                shell.redo();
            } else {
                shell.undo();
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
        shell.update();
        if (shell.active() && shell.active()->profile().name != profile) {
            const auto applied = shell.active()->profile();
            profile = applied.name;
            presenter.set_clear(applied.clear);
            context.request_frame();
        }
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
        auto* cad_space = dynamic_cast<cad::CadWorkspace*>(shell.active());
        expect(cad_space != nullptr, "Studio did not open in the CAD workspace");
        expect(profile == "CAD", "the CAD engine profile was not applied");
        expect(dock::panel_ids(shell.dock.layout()).size() == 3, "CAD panels are missing");

        push_click(window, center(context, shell.switcher("animation").element()));
        settle();
        expect(shell.active() == cad_space, "a planned workspace became active");

        push_click(window, center(context, cad_space->part_name.element()));
        settle();
        expect(window.text_input_active(), "focusing the editor did not start text input");
        push_text(window, true, "\xE3\x81\x8B"); // か, still composing
        settle();
        expect(need(cad_space->part_name.text()) == "Untitled", "composition changed committed text");
        push_text(window, false, "\xE6\x97\xA5\xE6\x9C\xAC"); // 日本
        settle();
        const auto edited = need(cad_space->part_name.text());
        expect(edited.find("\xE6\x97\xA5\xE6\x9C\xAC") != std::string::npos,
               "committed IME text did not reach the editor");
        expect(edited.find("\xE3\x81\x8B") == std::string::npos, "preedit text was committed");
        expect(document.state().name == edited, "the name edit did not reach the document");
        expect(presenter.frames() > idle, "the edit was not drawn");

        // In a focused text field the shortcuts edit the field's own text.
        push_shortcut(window, SDLK_Z, SDL_KMOD_GUI);
        settle();
        expect(need(cad_space->part_name.text()) != edited, "Cmd+Z did not undo typing in the text field");
        expect(document.state().name == need(cad_space->part_name.text()), "the field undo did not reach the document");
        push_shortcut(window, SDLK_Z, static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT));
        settle();
        expect(need(cad_space->part_name.text()) == edited && document.state().name == edited,
               "Shift+Cmd+Z did not redo typing in the text field");
        push_click(window, center(context, shell.undo_button.element()));
        settle();
        expect(document.state().name == "Untitled", "Undo did not revert the rename");
        expect(need(cad_space->part_name.text()) == "Untitled", "the editor did not follow undo");
        push_shortcut(window, SDLK_Z, static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT));
        settle();
        expect(document.state().name == edited, "Shift+Cmd+Z did not redo the rename");
        expect(need(shell.status.text()) == "Redo Rename part", "redo was not reported");

        need(shell.dock.dock("inspector", "model", dock::DropSide::center));
        settle();
        const auto moved = shell.dock.save();
        expect(moved != shell.default_layout(), "docking a panel did not change the layout");
        need(shell.dock.restore(shell.default_layout()));
        need(shell.dock.restore(moved));
        expect(shell.dock.save() == moved, "the layout did not round-trip");

        push_click(window, center(context, shell.switcher("probe").element()));
        settle();
        expect(shell.active() && shell.active()->id() == "probe", "switching workspace failed");
        expect(profile == "Probe", "the engine profile did not follow the workspace");
        expect(!cad_space->part_name.alive(), "CAD panels survived the switch");
        expect(document.state().name == edited, "switching workspace lost document state");
        push_click(window, center(context, shell.switcher("cad").element()));
        settle();
        expect(shell.active() == cad_space && profile == "CAD", "switching back to CAD failed");
        expect(shell.dock.save() == moved, "the CAD layout was not kept across switches");
        expect(need(cad_space->part_name.text()) == edited, "the rebuilt editor lost the name");

        push_click(window, center(context, shell.reset_layout.element()));
        settle();
        expect(shell.dock.save() == shell.default_layout(), "Reset layout did not restore");
        expect(need(shell.status.text()) == "Layout reset", "the reset was not reported");
        // Sketching: draw, close profiles, select, delete and undo.
        using Tool = cad::SketchEditor::Tool;
        auto& editor = cad_space->editor;
        const auto sketch = [&]() -> const model::Sketch* { return document.state().sketch(editor.sketch()); };
        expect(document.state().sketches.empty(), "a new document already had sketches");
        push_click(window, center(context, editor.tool_button(Tool::rectangle).element()));
        settle();
        expect(editor.tool() == Tool::rectangle, "the Rectangle tool button did nothing");
        push_click(window, editor.to_window({-20, -10}));
        push_click(window, editor.to_window({20, 10}));
        settle();
        expect(sketch() && sketch()->lines.size() == 4, "drawing a rectangle did not add four lines");
        expect(model::find_loops(*sketch()).closed.size() == 1, "the rectangle is not a closed profile");
        expect(cad_space->sketch_buttons.size() == 1, "the sketch list did not show the new sketch");
        push_shortcut(window, SDLK_C, SDL_KMOD_NONE);
        settle();
        expect(editor.tool() == Tool::circle, "the C shortcut did not pick the Circle tool");
        push_click(window, editor.to_window({0, 0}));
        push_click(window, editor.to_window({5, 0}));
        settle();
        expect(sketch()->circles.size() == 1 && model::find_loops(*sketch()).closed.size() == 2,
               "drawing a circle did not add a second profile");
        const auto circle = sketch()->circles[0].id;
        push_shortcut(window, SDLK_S, SDL_KMOD_NONE);
        push_click(window, editor.to_window({0, 5}));
        settle();
        expect(editor.selection() == std::vector{circle}, "clicking the circle did not select it");
        push_shortcut(window, SDLK_BACKSPACE, SDL_KMOD_NONE);
        settle();
        expect(sketch()->circles.empty() && editor.selection().empty(), "Backspace did not delete the circle");
        push_shortcut(window, SDLK_Z, SDL_KMOD_GUI);
        settle();
        expect(sketch()->circles.size() == 1, "Cmd+Z did not restore the deleted circle");
        for (int i = 0; i < 2; ++i) { push_shortcut(window, SDLK_Z, SDL_KMOD_GUI); }
        settle();
        expect(document.state().sketches.empty() && editor.sketch() == 0 && cad_space->sketch_buttons.empty(),
               "undoing the first drawing did not remove its sketch");
        push_click(window, center(context, cad_space->new_sketch.element()));
        settle();
        expect(document.state().sketches.size() == 1 && editor.sketch() == document.state().sketches[0].id,
               "New sketch did not create and activate a sketch");

        // Constraints: fix a rectangle's corner, dimension two sides, then
        // edit a dimension in its field and drag a free corner.
        using Action = cad::SketchEditor::Action;
        push_click(window, center(context, editor.tool_button(Tool::rectangle).element()));
        push_click(window, editor.to_window({0, 0}));
        push_click(window, editor.to_window({30, 20}));
        settle();
        expect(sketch()->lines.size() == 4 && sketch()->constraints.size() == 4,
               "the rectangle did not get horizontal and vertical constraints");
        expect(editor.status().status == model::SolveStatus::under && editor.status().freedom == 4,
               "a new rectangle should have four degrees of freedom");
        const auto corner = model::find_point(*sketch(), {0, 0}, 1e-6);
        const auto bottom = sketch()->lines[0].id;
        const auto right = sketch()->lines[1].id;
        push_shortcut(window, SDLK_S, SDL_KMOD_NONE);
        settle();
        const auto select = [&](std::initializer_list<model::Vec2> at) {
            bool first = true;
            for (const auto p : at) {
                SDL_SetModState(first ? SDL_KMOD_NONE : SDL_KMOD_SHIFT);
                push_click(window, editor.to_window(p));
                settle();
                first = false;
            }
            SDL_SetModState(SDL_KMOD_NONE);
        };
        select({{0, 0}});
        push_click(window, center(context, editor.action_button(Action::fixed).element()));
        settle();
        expect(editor.status().freedom == 2, "fixing a corner did not remove two degrees of freedom");
        select({{15, 0}});
        expect(editor.selection() == std::vector{bottom}, "clicking the bottom edge did not select it");
        push_click(window, center(context, editor.action_button(Action::dimension).element()));
        select({{30, 10}});
        push_click(window, center(context, editor.action_button(Action::dimension).element()));
        settle();
        expect(editor.status().status == model::SolveStatus::full, "two dimensions did not fully constrain");
        select({{15, 0}, {15, 20}});
        expect(editor.selection().size() == 2, "Shift-click did not extend the selection");
        push_click(window, center(context, editor.action_button(Action::parallel).element()));
        settle();
        expect(sketch()->constraints.size() == 7, "an over-constraining Parallel was accepted");
        expect(need(shell.status.text()).find("over-constrain") != std::string::npos,
               "the over-constraint was not reported");
        model::ObjectId width = 0;
        for (const auto& c : sketch()->constraints) {
            if (c.kind == model::ConstraintKind::distance && c.a == bottom) { width = c.id; }
        }
        auto field = editor.dimension_field(width);
        expect(field.alive() && need(field.text()) == "30", "the width field does not show 30");
        push_click(window, center(context, field.element()));
        settle();
        expect(cad_space->text_editing(context), "the dimension field did not take focus");
        for (int i = 0; i < 4; ++i) {
            push_shortcut(window, SDLK_BACKSPACE, SDL_KMOD_NONE);
            push_shortcut(window, SDLK_DELETE, SDL_KMOD_NONE);
        }
        push_text(window, false, "45");
        push_shortcut(window, SDLK_RETURN, SDL_KMOD_NONE);
        settle();
        const auto length = [&](model::ObjectId id) {
            const auto* l = sketch()->line(id);
            return model::distance(sketch()->at(l->start), sketch()->at(l->end));
        };
        expect(std::abs(length(bottom) - 45) < 1e-6, "editing the width field did not resize the rectangle");
        expect(sketch()->at(corner) == model::Vec2{0, 0}, "the fixed corner moved");
        expect(std::abs(length(right) - 20) < 1e-6, "the height changed with the width");
        expect(!cad_space->text_editing(context), "Enter did not hand focus back to the canvas");
        push_shortcut(window, SDLK_Z, SDL_KMOD_GUI);
        settle();
        expect(std::abs(length(bottom) - 30) < 1e-6 && need(field.text()) == "30",
               "undo did not restore the width and its field");
        std::cout << "studio smoke: frames=" << presenter.frames() << " name=" << edited << '\n';
    }
    context.set_host(nullptr);
    return 0;
}

} // namespace studio
