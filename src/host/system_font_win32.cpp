#include "host/system_font.hpp"
#include "studio/check.hpp"

#include <windows.h>

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <vector>

namespace studio {
namespace {

std::optional<truffle::gui::FontId> load_face(
    truffle::gui::TextEngine& engine, const LOGFONTW& description, float size) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) { throw std::runtime_error("could not create the system font DC"); }
    HFONT font = CreateFontIndirectW(&description);
    if (!font) {
        DeleteDC(dc);
        throw std::runtime_error("could not create the system UI font");
    }
    const HGDIOBJ previous = SelectObject(dc, font);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(font);
        DeleteDC(dc);
        throw std::runtime_error("could not select the system UI font");
    }
    struct Selection {
        HDC dc;
        HFONT font;
        HGDIOBJ previous;
        ~Selection() {
            SelectObject(dc, previous);
            DeleteObject(font);
            DeleteDC(dc);
        }
    } selection{dc, font, previous};
    const DWORD length = GetFontData(dc, 0, 0, nullptr, 0);
    std::vector<std::byte> bytes;
    bool read = false;
    if (length != GDI_ERROR && length > 0) {
        bytes.resize(length);
        read = GetFontData(dc, 0, 0, bytes.data(), length) == length;
    }
    if (!read) { return std::nullopt; }
    auto loaded = engine.load_font(bytes, size);
    if (!loaded.ok()) {
        std::fprintf(stderr, "System font load failed: %s\n", loaded.status().message.c_str());
        return std::nullopt;
    }
    return loaded.value();
}

} // namespace

truffle::gui::FontId load_system_font(truffle::gui::TextEngine& engine, float size) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        throw std::runtime_error("could not resolve the Windows system UI font");
    }
    const auto primary = load_face(engine, metrics.lfMessageFont, size);
    if (!primary) { throw std::runtime_error("could not load the Windows system UI font"); }
    std::vector<truffle::gui::FontId> fallbacks;
    for (const wchar_t* name : {L"Segoe UI", L"Nirmala UI", L"Yu Gothic UI",
                               L"Microsoft YaHei UI", L"Malgun Gothic"}) {
        LOGFONTW description{};
        description.lfCharSet = DEFAULT_CHARSET;
        description.lfWeight = FW_NORMAL;
        wcscpy_s(description.lfFaceName, name);
        if (const auto loaded = load_face(engine, description, size)) {
            fallbacks.push_back(*loaded);
        } else {
            std::fwprintf(stderr, L"Optional system font unavailable: %ls\n", name);
        }
    }
    need(engine.set_fallbacks(*primary, fallbacks));
    return *primary;
}

} // namespace studio
