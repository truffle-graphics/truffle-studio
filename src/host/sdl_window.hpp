#pragma once

#include "truffle/gui/host.hpp"
#include "truffle/gui/input.hpp"
#include "truffle/rhi/types.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

struct SDL_Window;
struct SDL_Cursor;
union SDL_Event;

namespace studio {

struct WindowMetrics {
    truffle::gui::Size logical;
    float scale = 1.0F;
    truffle::rhi::Extent2D pixels;
};

// One SDL3 window with a CAMetalLayer, translating SDL input into Truffle GUI
// input and serving the GUI's platform requests (cursor, clipboard, IME).
class SdlWindow final : public truffle::gui::Host {
public:
    using Forward = std::function<void(const truffle::gui::InputEvent&)>;

    SdlWindow(const char* title, int width, int height);
    ~SdlWindow() override;
    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    [[nodiscard]] SDL_Window* native() const noexcept { return window_; }
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] void* metal_layer() const noexcept { return layer_; }
    [[nodiscard]] bool text_input_active() const noexcept { return text_input_; }

    // Empty while minimized or without a drawable area.
    [[nodiscard]] std::optional<WindowMetrics> metrics() const;

    // Forwards GUI input for events addressed to this window; ignores others.
    void translate(const SDL_Event& event, const Forward& forward);

    void set_cursor(truffle::gui::Cursor cursor) override;
    [[nodiscard]] std::string clipboard_text() override;
    void set_clipboard_text(std::string_view text) override;
    void set_text_input_area(std::optional<truffle::gui::Rect> area) override;
    void set_text_input_state(std::optional<truffle::gui::TextInputState> state) override;

private:
    SDL_Window* window_ = nullptr;
    void* view_ = nullptr; // SDL_MetalView
    void* layer_ = nullptr;
    std::uint32_t id_ = 0;
    bool composing_ = false;
    bool text_input_ = false;
    std::array<SDL_Cursor*, 9> cursors_{};
};

} // namespace studio
