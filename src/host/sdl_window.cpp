#include "host/sdl_window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace studio {
namespace gui = truffle::gui;

namespace {

gui::Modifiers modifiers_of(SDL_Keymod mod) {
    gui::Modifiers result = gui::Modifiers::none;
    if ((mod & SDL_KMOD_SHIFT) != 0) { result = result | gui::Modifiers::shift; }
    if ((mod & SDL_KMOD_CTRL) != 0) { result = result | gui::Modifiers::control; }
    if ((mod & SDL_KMOD_ALT) != 0) { result = result | gui::Modifiers::alt; }
    if ((mod & SDL_KMOD_GUI) != 0) { result = result | gui::Modifiers::super; }
    return result;
}

gui::PointerButton button_of(std::uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return gui::PointerButton::primary;
    case SDL_BUTTON_RIGHT: return gui::PointerButton::secondary;
    case SDL_BUTTON_MIDDLE: return gui::PointerButton::middle;
    case SDL_BUTTON_X1: return gui::PointerButton::back;
    case SDL_BUTTON_X2: return gui::PointerButton::forward;
    default: return gui::PointerButton::none;
    }
}

gui::Key key_of(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return static_cast<gui::Key>(static_cast<int>(gui::Key::a) + (code - SDL_SCANCODE_A));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        return static_cast<gui::Key>(
            static_cast<int>(gui::Key::digit1) + (code - SDL_SCANCODE_1));
    }
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
        return static_cast<gui::Key>(static_cast<int>(gui::Key::f1) + (code - SDL_SCANCODE_F1));
    }
    switch (code) {
    case SDL_SCANCODE_0: return gui::Key::digit0;
    case SDL_SCANCODE_SPACE: return gui::Key::space;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return gui::Key::enter;
    case SDL_SCANCODE_ESCAPE: return gui::Key::escape;
    case SDL_SCANCODE_TAB: return gui::Key::tab;
    case SDL_SCANCODE_BACKSPACE: return gui::Key::backspace;
    case SDL_SCANCODE_DELETE: return gui::Key::delete_forward;
    case SDL_SCANCODE_INSERT: return gui::Key::insert;
    case SDL_SCANCODE_HOME: return gui::Key::home;
    case SDL_SCANCODE_END: return gui::Key::end;
    case SDL_SCANCODE_PAGEUP: return gui::Key::page_up;
    case SDL_SCANCODE_PAGEDOWN: return gui::Key::page_down;
    case SDL_SCANCODE_LEFT: return gui::Key::left;
    case SDL_SCANCODE_RIGHT: return gui::Key::right;
    case SDL_SCANCODE_UP: return gui::Key::up;
    case SDL_SCANCODE_DOWN: return gui::Key::down;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return gui::Key::shift;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return gui::Key::control;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return gui::Key::alt;
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI: return gui::Key::super;
    default: return gui::Key::unknown;
    }
}

// SDL reports composition selections in code points; Truffle uses UTF-8 bytes.
std::size_t byte_offset(std::string_view text, std::int32_t code_points) {
    std::size_t offset = 0;
    for (; code_points > 0 && offset < text.size(); --code_points) {
        ++offset;
        while (offset < text.size() &&
               (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U) {
            ++offset;
        }
    }
    return offset;
}

SDL_SystemCursor system_cursor(gui::Cursor cursor) {
    switch (cursor) {
    case gui::Cursor::text: return SDL_SYSTEM_CURSOR_TEXT;
    case gui::Cursor::hand: return SDL_SYSTEM_CURSOR_POINTER;
    case gui::Cursor::crosshair: return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case gui::Cursor::move: return SDL_SYSTEM_CURSOR_MOVE;
    case gui::Cursor::resize_horizontal: return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case gui::Cursor::resize_vertical: return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case gui::Cursor::not_allowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default: return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

} // namespace

SdlWindow::SdlWindow(const char* title, int width, int height) {
    window_ = SDL_CreateWindow(title, width, height,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_ == nullptr) {
        throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
    }
    view_ = SDL_Metal_CreateView(window_);
    layer_ = view_ != nullptr ? SDL_Metal_GetLayer(view_) : nullptr;
    if (layer_ == nullptr) {
        const std::string error = SDL_GetError();
        if (view_ != nullptr) { SDL_Metal_DestroyView(view_); }
        SDL_DestroyWindow(window_);
        throw std::runtime_error("SDL Metal view creation failed: " + error);
    }
    id_ = SDL_GetWindowID(window_);
}

SdlWindow::~SdlWindow() {
    if (text_input_) { SDL_StopTextInput(window_); }
    for (SDL_Cursor* cursor : cursors_) {
        if (cursor != nullptr) { SDL_DestroyCursor(cursor); }
    }
    SDL_Metal_DestroyView(view_);
    SDL_DestroyWindow(window_);
}

std::optional<WindowMetrics> SdlWindow::metrics() const {
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) { return std::nullopt; }
    int width = 0;
    int height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window_, &width, &height) ||
        !SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height) ||
        width < 1 || height < 1 || pixel_width < 1 || pixel_height < 1) {
        return std::nullopt;
    }
    return WindowMetrics{
        .logical = {static_cast<float>(width), static_cast<float>(height)},
        .scale = static_cast<float>(pixel_width) / static_cast<float>(width),
        .pixels = {static_cast<std::uint32_t>(pixel_width),
                   static_cast<std::uint32_t>(pixel_height)}};
}

void SdlWindow::translate(const SDL_Event& event, const Forward& forward) {
    const auto modifiers = [] { return modifiers_of(SDL_GetModState()); };
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.windowID != id_) { return; }
        forward(gui::PointerEvent{.action = gui::PointerAction::move,
                                  .position = {event.motion.x, event.motion.y},
                                  .modifiers = modifiers()});
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.windowID != id_) { return; }
        forward(gui::PointerEvent{
            .action = event.button.down ? gui::PointerAction::down : gui::PointerAction::up,
            .position = {event.button.x, event.button.y},
            .button = button_of(event.button.button),
            .modifiers = modifiers()});
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.windowID != id_) { return; }
        // Match native Cocoa deltas: SDL negates X and scales trackpad deltas by 0.1.
        forward(gui::WheelEvent{.position = {event.wheel.mouse_x, event.wheel.mouse_y},
                                .delta_x = -event.wheel.x, .delta_y = event.wheel.y,
                                .modifiers = modifiers()});
        return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        if (event.window.windowID != id_) { return; }
        forward(gui::PointerEvent{.action = gui::PointerAction::leave});
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID != id_) { return; }
        composing_ = false;
        forward(gui::FocusEvent{.focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED});
        return;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event.key.windowID != id_) { return; }
        const auto mods = modifiers_of(event.key.mod);
        // While an input method composes, it owns plain key presses.
        if (event.key.down && composing_ && !gui::has(mods, gui::Modifiers::super)) { return; }
        forward(gui::KeyEvent{.key = key_of(event.key.scancode), .down = event.key.down,
                              .repeat = event.key.repeat, .modifiers = mods,
                              .native = static_cast<std::uint32_t>(event.key.scancode)});
        return;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.windowID != id_) { return; }
        composing_ = false;
        forward(gui::TextEvent{.text = event.text.text});
        return;
    case SDL_EVENT_TEXT_EDITING: {
        if (event.edit.windowID != id_) { return; }
        std::string preedit = event.edit.text != nullptr ? event.edit.text : "";
        const std::size_t begin = event.edit.start < 0
            ? preedit.size() : byte_offset(preedit, event.edit.start);
        const std::size_t end = event.edit.length < 0
            ? begin : begin + byte_offset(std::string_view(preedit).substr(begin),
                                          event.edit.length);
        composing_ = !preedit.empty();
        forward(gui::CompositionEvent{.preedit = std::move(preedit),
                                      .selection_begin = begin, .selection_end = end});
        return;
    }
    default:
        return;
    }
}

void SdlWindow::set_cursor(gui::Cursor cursor) {
    if (cursor == gui::Cursor::hidden) {
        SDL_HideCursor();
        return;
    }
    const auto index = static_cast<std::size_t>(cursor);
    if (index < cursors_.size() && cursors_[index] == nullptr) {
        cursors_[index] = SDL_CreateSystemCursor(system_cursor(cursor));
    }
    if (index < cursors_.size() && cursors_[index] != nullptr) {
        SDL_SetCursor(cursors_[index]);
    }
    SDL_ShowCursor();
}

std::string SdlWindow::clipboard_text() {
    char* text = SDL_GetClipboardText();
    std::string result = text != nullptr ? text : "";
    SDL_free(text);
    return result;
}

void SdlWindow::set_clipboard_text(std::string_view text) {
    SDL_SetClipboardText(std::string(text).c_str());
}

void SdlWindow::set_text_input_area(std::optional<gui::Rect> area) {
    if (!area) { return; }
    const SDL_Rect rect{static_cast<int>(std::floor(area->x)), static_cast<int>(std::floor(area->y)),
                        std::max(1, static_cast<int>(std::ceil(area->width))),
                        std::max(1, static_cast<int>(std::ceil(area->height)))};
    SDL_SetTextInputArea(window_, &rect, 0);
}

void SdlWindow::set_text_input_state(std::optional<gui::TextInputState> state) {
    // SDL tracks marked text itself; Studio only switches text input on and off.
    if (state && !text_input_) {
        text_input_ = SDL_StartTextInput(window_);
    } else if (!state && text_input_) {
        SDL_StopTextInput(window_);
        text_input_ = false;
        composing_ = false;
    }
}

} // namespace studio
