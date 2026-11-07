#include "studio/app.hpp"

#include "host/sdl_window.hpp"
#include "model/document.hpp"
#include "studio/check.hpp"
#include "studio/smoke_readback.hpp"

#include "truffle/gui/graphics.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace studio {
namespace gui = truffle::gui;

namespace {
constexpr gui::Color background{13.0F / 255, 13.0F / 255, 15.0F / 255, 1};
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
        if (smoke) { SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); }
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
        }
    }
    ~SdlSession() { SDL_Quit(); }
    SdlSession(const SdlSession&) = delete;
    SdlSession& operator=(const SdlSession&) = delete;
};

std::size_t draw_if_needed(gui::Graphics& graphics, gui::Context& context,
                           const SdlWindow& window) {
    if (!window.metrics()) { return 0; }
    const auto now = gui::Context::Clock::now();
    if (!graphics.needs_frame(context, now)) { return 0; }
    const auto status = graphics.draw(context, now);
    const bool presented = graphics.last_frame_stats().presented;
    if (status.code == truffle::base::StatusCode::surface_lost) {
        const auto metrics = window.metrics();
        if (!metrics) {
            return presented ? 1 : 0;
        }
        need(graphics.replace_window(context, window.window_handle(), metrics->pixels,
                                     metrics->scale));
        context.request_frame();
    } else if (status.code == truffle::base::StatusCode::device_lost) {
        throw std::runtime_error(
            "Truffle GUI device was lost; restart Studio to recreate its GPU consumers");
    } else if (status.code == truffle::base::StatusCode::out_of_date ||
               status.code == truffle::base::StatusCode::suboptimal) {
        context.request_frame();
    } else {
        need(status);
    }
    return presented ? 1 : 0;
}

void check_blank_for_smoke(gui::Graphics& graphics, gui::PixelSize pixels) {
    need(graphics.wait_idle());
    const auto stats = graphics.last_frame_stats();
    expect(stats.presented && stats.batches == 0,
           "the client was not a blank GUI-only presentation");
    verify_clear_pixels(graphics, pixels, background);
}

std::int32_t wait_timeout(const gui::Graphics& graphics, const gui::Context& context,
                          const SdlWindow& window,
                          std::optional<std::chrono::milliseconds> cap) {
    using namespace std::chrono;
    if (!window.metrics()) {
        return cap ? static_cast<std::int32_t>(cap->count()) : -1;
    }
    const auto now = gui::Context::Clock::now();
    auto timeout = cap.value_or(milliseconds::max());
    if (const auto next = context.next_update()) {
        const auto until = std::chrono::ceil<milliseconds>(*next - now);
        timeout = std::min(timeout, std::max(milliseconds::zero(), until));
    }
    if (graphics.needs_frame(context, now)) { return 0; }
    if (timeout == milliseconds::max()) { return -1; }
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        timeout.count(), 0, std::numeric_limits<std::int32_t>::max()));
}

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
    SdlSession session(options.smoke);
    SdlWindow window("Truffle Studio", 1280, 800);
    const auto initial = window.metrics();
    if (!initial) { throw std::runtime_error("the Studio window has no drawable area"); }

    auto graphics = need(gui::Graphics::create({
        .window = window.window_handle(),
        .pixels = initial->pixels,
        .dpi_scale = initial->scale,
        .enable_validation = options.smoke,
        .clear = background,
    }));
    gui::Context context;
    context.set_host(&window);
    need(context.set_viewport(
        {static_cast<float>(initial->pixels.width) / initial->scale,
         static_cast<float>(initial->pixels.height) / initial->scale},
        initial->scale));
    model::Document document;

    bool running = true;
    std::size_t frames = 0;
    std::optional<WindowMetrics> configured = initial;
    const auto forward_input = [&](const gui::InputEvent& input) {
        need(context.handle(input));
    };
    const auto dispatch = [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == window.id())) {
            running = false;
            return;
        }
        if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             event.type == SDL_EVENT_WINDOW_EXPOSED ||
             event.type == SDL_EVENT_WINDOW_RESTORED ||
             event.type == SDL_EVENT_WINDOW_SHOWN) && event.window.windowID == window.id()) {
            context.request_frame();
        }
        window.translate(event, forward_input);
    };
    const auto iterate = [&](std::optional<std::chrono::milliseconds> cap) {
        SDL_Event event{};
        if (SDL_WaitEventTimeout(&event, wait_timeout(graphics, context, window, cap))) {
            dispatch(event);
            while (SDL_PollEvent(&event)) { dispatch(event); }
        }
        if (!running) { return; }

        const auto metrics = window.metrics();
        if (metrics) {
            const bool changed = !configured ||
                configured->pixels != metrics->pixels || configured->scale != metrics->scale;
            if (changed) {
                need(graphics.resize(context, metrics->pixels, metrics->scale));
                configured = metrics;
            }
        } else if (configured) {
            need(graphics.resize(context, {}, configured->scale));
            configured.reset();
        }
        if (metrics) { frames += draw_if_needed(graphics, context, window); }
    };

    if (!options.smoke) {
        while (running) { iterate(std::nullopt); }
    } else {
        const auto settle = [&] {
            for (int i = 0; i < 20; ++i) { iterate(std::chrono::milliseconds(5)); }
        };
        const auto check_blank = [&] {
            context.request_frame();
            settle();
            const auto metrics = window.metrics();
            expect(metrics.has_value(), "the smoke window has no drawable area");
            check_blank_for_smoke(graphics, metrics->pixels);
            expect(!window.text_input_active(), "the blank window started a text editor");
        };
        settle();
        expect(frames > 0, "the window presented no frame");
        const auto idle = frames;
        settle();
        expect(frames == idle, "an idle blank window kept drawing");
        check_blank();

#if defined(_WIN32)
        expect(SDL_MinimizeWindow(window.native()), "could not minimize the smoke window");
        settle();
        expect(!window.metrics(), "a minimized window remained drawable");
        const auto minimized = frames;
        context.request_frame();
        settle();
        expect(frames == minimized, "a minimized blank window kept drawing");
        expect(SDL_RestoreWindow(window.native()), "could not restore the smoke window");
        settle();
        expect(window.metrics().has_value() && frames > minimized,
               "restoring the window did not resume presentation");
        check_blank();
#endif
        for (const auto size : {gui::PixelSize{1024, 700}, gui::PixelSize{1600, 900},
                                gui::PixelSize{1280, 800}}) {
            const auto before = frames;
            expect(SDL_SetWindowSize(window.native(), static_cast<int>(size.width),
                                     static_cast<int>(size.height)),
                   "could not resize the smoke window");
            settle();
            expect(frames > before, "resizing did not redraw the blank window");
            const auto resized = window.metrics();
            expect(resized.has_value(), "resized window has no drawable area");
            check_blank_for_smoke(graphics, resized->pixels);
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
        const auto before_input = frames;
        settle();
        expect(document.state() == saved && document.revision() == revision &&
                   document.can_undo() && !document.can_redo(),
               "legacy input changed the preserved document");
        expect(frames == before_input, "inert client input requested a frame");
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
        std::cout << "studio blank smoke: frames=" << frames
                  << " no GUI batches; input inert; lifecycle passed\n";
    }
    need(graphics.wait_idle());
    return 0;
}

} // namespace studio
