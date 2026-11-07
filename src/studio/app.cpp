#include "studio/app.hpp"

#include "host/sdl_window.hpp"
#include "host/system_font.hpp"
#include "model/document.hpp"
#include "studio/check.hpp"
#include "studio/menu_bar.hpp"
#include "studio/smoke_readback.hpp"

#include "truffle/gui/graphics.hpp"
#include "truffle/gui/text_edit.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

gui::Point center(gui::Context& context, gui::ElementId element) {
    const auto bounds = need(context.absolute_bounds(element));
    expect(bounds.width > 0 && bounds.height > 0, "a menu control has no layout");
    return {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
}

void check_menu_for_smoke(gui::Graphics& graphics, gui::Context& context,
                          const MainMenu& menu, const SdlWindow& window, bool readback) {
    const auto metrics = window.metrics();
    expect(metrics.has_value(), "the smoke window has no drawable area");
    need(graphics.wait_idle());
    const auto stats = graphics.last_frame_stats();
    expect(stats.presented && stats.batches > 0, "the menu bar was not presented");
    const auto bar = need(context.absolute_bounds(menu.element()));
    expect(std::abs(bar.x) < 0.01F && std::abs(bar.y) < 0.01F &&
               std::abs(bar.width - metrics->logical.width) < 1.0F &&
               bar.height > 0 && bar.height < metrics->logical.height,
           "the menu bar did not span the top of the client");
    for (const auto trigger : {menu.file().trigger(), menu.edit().trigger()}) {
        const auto bounds = need(context.absolute_bounds(trigger));
        expect(bounds.width > 0 && bounds.height > 0 && bounds.x >= bar.x &&
                   bounds.y >= bar.y &&
                   bounds.x + bounds.width <= bar.x + bar.width + 0.01F &&
                   bounds.y + bounds.height <= bar.y + bar.height + 0.01F,
               "a menu trigger is outside the menu bar");
    }
    expect(need(context.semantics(menu.element())).role == gui::Context::Role::menu_bar &&
               need(context.semantics(menu.file().trigger())).label == "File" &&
               need(context.semantics(menu.edit().trigger())).label == "Edit",
           "the menu bar has incorrect semantics");
    expect(!window.text_input_active(), "the menu-only window started a text editor");
    if (readback) {
        MenuReadbackRegions regions{
            .scale = metrics->scale,
            .bar = bar,
            .file_trigger = need(context.absolute_bounds(menu.file().trigger())),
            .edit_trigger = need(context.absolute_bounds(menu.edit().trigger())),
        };
        if (menu.file().is_open()) {
            regions.popup = need(context.absolute_bounds(menu.file().element()));
            regions.popup_item = need(context.absolute_bounds(menu.quit_item()));
        }
        verify_menu_pixels(graphics, context, metrics->pixels, background, regions);
    }
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

void push_key(const SdlWindow& window, SDL_Keycode key, SDL_Keymod mod,
              bool repeat = false, SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window.id();
    event.key.key = key;
    event.key.scancode = scancode == SDL_SCANCODE_UNKNOWN
        ? SDL_GetScancodeFromKey(key, nullptr) : scancode;
    event.key.mod = mod;
    event.key.down = true;
    event.key.repeat = repeat;
    expect(SDL_PushEvent(&event), "could not queue a keyboard event");
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.repeat = false;
    expect(SDL_PushEvent(&event), "could not queue a keyboard release");
}

void push_click(const SdlWindow& window, gui::Point point) {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = window.id();
    motion.motion.x = point.x;
    motion.motion.y = point.y;
    expect(SDL_PushEvent(&motion), "could not queue a pointer motion");
    for (const bool down : {true, false}) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = window.id();
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = point.x;
        event.button.y = point.y;
        expect(SDL_PushEvent(&event), "could not queue a pointer click");
    }
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
    auto& text = need(graphics.text()).get();
    const auto font = load_system_font(text, 14.0F);
    MainMenu menu(context, {&text, font, graphics.text_atlas()}, document,
                  [&] { running = false; });
    std::size_t frames = 0;
    std::optional<WindowMetrics> configured = initial;
    const auto forward_input = [&](const gui::InputEvent& input) {
        need(menu.sync());
        need(context.handle(input));
        need(menu.sync());
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
        const auto enabled = [&](gui::ElementId element) {
            return need(context.interaction(element)).enabled;
        };

        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = window.id();
        expect(SDL_PushEvent(&focus), "could not queue a window focus event");
        settle();
        expect(frames > 0, "the window presented no frame");
        check_menu_for_smoke(graphics, context, menu, window, true);
        const auto idle = frames;
        settle();
        expect(frames == idle, "an idle menu window kept drawing");
        expect(!enabled(menu.undo_item()) && !enabled(menu.redo_item()),
               "empty document history left menu commands enabled");

        push_click(window, center(context, menu.file().trigger()));
        settle();
        expect(menu.file().is_open(), "pointer input did not open File");
        check_menu_for_smoke(graphics, context, menu, window, true);
        push_key(window, SDLK_ESCAPE, SDL_KMOD_NONE);
        settle();
        expect(!menu.file().is_open() && context.focused() == menu.file().trigger(),
               "Escape did not dismiss File and restore focus");
        push_key(window, SDLK_RIGHT, SDL_KMOD_NONE);
        settle();
        expect(context.focused() == menu.edit().trigger(),
               "keyboard traversal did not reach Edit");
        push_key(window, SDLK_DOWN, SDL_KMOD_NONE);
        settle();
        expect(menu.edit().is_open(), "keyboard input did not open Edit");
        push_key(window, SDLK_ESCAPE, SDL_KMOD_NONE);
        settle();
        expect(!menu.edit().is_open(), "Escape did not dismiss Edit");

        expect(document.edit("Smoke document", [](model::DocumentState& state) {
            state.name = "Preserved document";
        }), "could not prepare document history");
        expect(enabled(menu.undo_item()) && !enabled(menu.redo_item()),
               "a document edit did not enable Undo");
        push_click(window, center(context, menu.edit().trigger()));
        settle();
        expect(menu.edit().is_open(), "pointer input did not open Edit");
        push_click(window, center(context, menu.undo_item()));
        settle();
        expect(document.state().name == "Untitled" && !menu.edit().is_open() &&
                   !enabled(menu.undo_item()) && enabled(menu.redo_item()),
               "the Undo menu action did not update document history");

#if defined(_WIN32)
        constexpr SDL_Keycode redo_key = SDLK_Y;
        constexpr SDL_Keymod redo_modifier = SDL_KMOD_CTRL;
#else
        constexpr SDL_Keycode redo_key = SDLK_Z;
        constexpr SDL_Keymod redo_modifier =
            static_cast<SDL_Keymod>(SDL_KMOD_GUI | SDL_KMOD_SHIFT);
#endif
        push_key(window, redo_key, redo_modifier);
        settle();
        expect(document.state().name == "Preserved document" &&
                   enabled(menu.undo_item()) && !enabled(menu.redo_item()),
               "the Redo shortcut did not restore document history");
        push_key(window, SDLK_Z, shortcut_modifier, true);
        push_key(window, SDLK_Z, static_cast<SDL_Keymod>(shortcut_modifier | SDL_KMOD_ALT));
        settle();
        expect(document.state().name == "Preserved document",
               "a repeated or mismatched shortcut changed document history");
        push_key(window, SDLK_Z, shortcut_modifier);
        settle();
        expect(document.state().name == "Untitled", "the Undo shortcut did not undo");
        push_key(window, redo_key, redo_modifier);
        settle();
        expect(document.state().name == "Preserved document", "the Redo shortcut did not redo");

        push_key(window, SDLK_A, shortcut_modifier, false, SDL_SCANCODE_Z);
        settle();
        expect(document.state().name == "Preserved document",
               "a physical key with a different logical label triggered Undo");
        push_click(window, center(context, menu.file().trigger()));
        settle();
        expect(menu.file().is_open(), "File did not open before the Undo shortcut");
        push_key(window, SDLK_Z, shortcut_modifier, false, SDL_SCANCODE_A);
        settle();
        expect(document.state().name == "Untitled" && !menu.file().is_open(),
               "the layout-aware Undo shortcut did not dismiss File and undo");
        push_key(window, redo_key, redo_modifier);
        settle();
        expect(document.state().name == "Preserved document", "Redo did not restore the document");

        need(context.focus(menu.edit().trigger()));
        push_key(window, SDLK_DOWN, SDL_KMOD_NONE);
        settle();
        expect(menu.edit().is_open(), "keyboard input did not reopen Edit");
        push_key(window, SDLK_RETURN, SDL_KMOD_NONE);
        settle();
        expect(document.state().name == "Untitled" && !menu.edit().is_open(),
               "keyboard activation did not invoke Undo");
        push_key(window, redo_key, redo_modifier);
        settle();
        expect(document.state().name == "Preserved document", "Redo did not restore the document");

        auto editor = need(gui::TextEdit::create(context, context.root(), {
            .text = "A",
            .text_engine = &text,
            .font = font,
            .atlas = graphics.text_atlas(),
        }));
        need(context.focus(editor.element()));
        need(editor.select({1, 1}));
        expect(window.text_input_active(), "the focused editor did not start native text input");
        SDL_Event edit_input{};
        edit_input.type = SDL_EVENT_TEXT_INPUT;
        edit_input.text.windowID = window.id();
        edit_input.text.text = "B";
        expect(SDL_PushEvent(&edit_input), "could not queue editor text");
        settle();
        expect(need(editor.text()) == "AB", "the editor did not receive text");
        push_key(window, SDLK_Z, shortcut_modifier, false, SDL_SCANCODE_Z);
        settle();
        expect(need(editor.text()) == "A" && document.state().name == "Preserved document",
               "the document shortcut stole a text editor's Undo");
        push_key(window, redo_key, redo_modifier, false,
                 redo_key == SDLK_Y ? SDL_SCANCODE_Y : SDL_SCANCODE_Z);
        settle();
        expect(need(editor.text()) == "AB" && document.state().name == "Preserved document",
               "the document shortcut stole a text editor's Redo");
        need(context.destroy(editor.element()));
        expect(!window.text_input_active(), "destroying the editor left text input active");

        document.begin("Pending edit");
        need(menu.sync());
        expect(!enabled(menu.undo_item()) && !enabled(menu.redo_item()),
               "an open edit left history commands enabled");
        document.cancel();
        expect(enabled(menu.undo_item()) && !enabled(menu.redo_item()),
               "finishing an edit did not restore history command state");
        settle();

#if defined(_WIN32)
        expect(SDL_MinimizeWindow(window.native()), "could not minimize the smoke window");
        settle();
        expect(!window.metrics(), "a minimized window remained drawable");
        const auto minimized = frames;
        context.request_frame();
        settle();
        expect(frames == minimized, "a minimized window kept drawing");
        expect(SDL_RestoreWindow(window.native()), "could not restore the smoke window");
        settle();
        expect(window.metrics().has_value() && frames > minimized,
               "restoring the window did not resume presentation");
        check_menu_for_smoke(graphics, context, menu, window, false);
#endif
        for (const auto size : {gui::PixelSize{1024, 700}, gui::PixelSize{1600, 900},
                                gui::PixelSize{1280, 800}}) {
            const auto before = frames;
            expect(SDL_SetWindowSize(window.native(), static_cast<int>(size.width),
                                     static_cast<int>(size.height)),
                   "could not resize the smoke window");
            settle();
            expect(frames > before, "resizing did not redraw the window");
            const auto resized = window.metrics();
            expect(resized.has_value(), "resized window has no drawable area");
            check_menu_for_smoke(graphics, context, menu, window, false);
        }

        need(context.focus({}));
        push_click(window, {640, 400});
        settle();
        const auto saved = document.state();
        const auto revision = document.revision();
        for (const auto point : {std::array{24.0F, 96.0F}, std::array{1250.0F, 96.0F},
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
                               SDLK_BACKSPACE, SDLK_DELETE, SDLK_ESCAPE, SDLK_RETURN}) {
            push_key(window, key, SDL_KMOD_NONE);
        }
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
        SDL_Event text_event{};
        text_event.type = SDL_EVENT_TEXT_INPUT;
        text_event.text.windowID = window.id();
        text_event.text.text = "No editor";
        expect(SDL_PushEvent(&text_event), "could not queue a text event");
        const auto before_input = frames;
        settle();
        expect(document.state() == saved && document.revision() == revision &&
                   document.can_undo() && !document.can_redo(),
               "inactive client input changed the preserved document");
        expect(frames == before_input, "inactive client input requested a frame");

        push_click(window, center(context, menu.file().trigger()));
        settle();
        expect(menu.file().is_open(), "File did not open for Quit");
        push_click(window, center(context, menu.quit_item()));
        settle();
        expect(!running, "the Quit menu action did not close Studio");
        running = true;
        push_key(window, SDLK_Q, shortcut_modifier, true);
        settle();
        expect(running, "a repeated Quit shortcut closed Studio");
        push_key(window, SDLK_Q, shortcut_modifier);
        settle();
        expect(!running, "the Quit shortcut did not close Studio");
        running = true;

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
        std::cout << "studio menu smoke: frames=" << frames
                  << " menu actions, shortcuts, inactive client and lifecycle passed\n";
    }
    need(graphics.wait_idle());
    return 0;
}

} // namespace studio
