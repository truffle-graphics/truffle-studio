#include "test.hpp"
#include "host/sdl_window.hpp"
#include "host/system_font.hpp"
#include "studio/check.hpp"

#include <SDL3/SDL.h>

#include <variant>

namespace gui = truffle::gui;

TEST(win32_host_input) {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    {
        studio::SdlWindow window("Studio host test", 640, 480);
        SDL_HideWindow(window.native());
        CHECK(window.window_handle().kind == gui::WindowKind::win32);
        CHECK(window.window_handle().handle != nullptr);
        CHECK(window.metrics().has_value());
        std::vector<gui::InputEvent> inputs;
        const auto forward = [&](const gui::InputEvent& input) { inputs.push_back(input); };
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = window.id() + 1;
        event.key.scancode = SDL_SCANCODE_Z;
        event.key.down = true;
        window.translate(event, forward);
        CHECK(inputs.empty());
        event.key.windowID = window.id();
        event.key.mod = SDL_KMOD_CTRL;
        window.translate(event, forward);
        CHECK(inputs.size() == 1);
        const auto& key = std::get<gui::KeyEvent>(inputs.back());
        CHECK(key.key == gui::Key::z);
        CHECK(gui::has(key.modifiers, gui::Modifiers::control));

        event = {};
        event.type = SDL_EVENT_TEXT_EDITING;
        event.edit.windowID = window.id();
        event.edit.text = "\xE6\x97\xA5\xE6\x9C\xAC";
        event.edit.start = 1;
        event.edit.length = 1;
        window.translate(event, forward);
        const auto& composition = std::get<gui::CompositionEvent>(inputs.back());
        CHECK(composition.selection_begin == 3);
        CHECK(composition.selection_end == 6);
        const auto composing = inputs.size();
        event = {};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = window.id();
        event.key.scancode = SDL_SCANCODE_A;
        event.key.down = true;
        window.translate(event, forward);
        CHECK(inputs.size() == composing);
        event.key.mod = SDL_KMOD_CTRL;
        window.translate(event, forward);
        CHECK(inputs.size() == composing + 1);
        event = {};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = window.id();
        event.text.text = "\xE6\x97\xA5\xE6\x9C\xAC";
        window.translate(event, forward);
        CHECK(std::get<gui::TextEvent>(inputs.back()).text == event.text.text);

        const std::string clipboard = window.clipboard_text();
        window.set_clipboard_text("Studio UTF-8 \xE6\x97\xA5\xE6\x9C\xAC");
        const std::string actual = window.clipboard_text();
        window.set_clipboard_text(clipboard);
        CHECK(actual == "Studio UTF-8 \xE6\x97\xA5\xE6\x9C\xAC");
        window.set_cursor(gui::Cursor::resize_horizontal);
        window.set_cursor(gui::Cursor::resize_vertical);
        window.set_cursor(gui::Cursor::text);
        window.set_text_input_state(gui::TextInputState{});
        CHECK(window.text_input_active());
        window.set_text_input_area(gui::Rect{10, 10, 100, 20});
        window.set_text_input_state(std::nullopt);
        CHECK(!window.text_input_active());
    }

    SDL_Quit();
}

TEST(win32_system_font) {
    gui::TextEngine text;
    const auto font = studio::load_system_font(text, 13);
    for (const char* sample : {"Studio", "\xD7\x90", "\xD8\xA7", "\xE6\x97\xA5\xE6\x9C\xAC"}) {
        const auto shaped = studio::need(text.shape(font, sample));
        CHECK(!shaped.glyphs.empty());
        for (const auto& glyph : shaped.glyphs) { CHECK(glyph.id != 0); }
    }
}
