#include "host/system_font.hpp"

#include "studio/check.hpp"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio {
namespace {

// Loads each font file once; `seen` holds the paths already loaded.
std::optional<truffle::gui::FontId> load_face(truffle::gui::TextEngine& engine, CTFontRef font,
                                              float size, std::set<std::string>& seen) {
    CFTypeRef attribute = CTFontCopyAttribute(font, kCTFontURLAttribute);
    if (attribute == nullptr || CFGetTypeID(attribute) != CFURLGetTypeID()) {
        if (attribute != nullptr) { CFRelease(attribute); }
        return std::nullopt;
    }
    NSURL* url = CFBridgingRelease(attribute);
    if (!seen.insert(url.path.UTF8String).second) { return std::nullopt; }
    NSData* bytes = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:nil];
    if (bytes == nil) { return std::nullopt; }
    auto loaded = engine.load_font({static_cast<const std::byte*>(bytes.bytes), bytes.length}, size);
    if (!loaded.ok()) { return std::nullopt; }
    return loaded.value();
}

} // namespace

truffle::gui::FontId load_system_font(truffle::gui::TextEngine& engine, float size) {
    CTFontRef system = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
    if (system == nullptr) { throw std::runtime_error("could not resolve the system UI font"); }
    std::set<std::string> seen;
    const auto primary = load_face(engine, system, size, seen);
    if (!primary) {
        CFRelease(system);
        throw std::runtime_error("could not load the system UI font");
    }
    // Best-effort fallbacks for common scripts. Collections (.ttc) load their
    // first face, so a missing script degrades to missing glyphs, not failure.
    std::vector<truffle::gui::FontId> fallbacks;
    for (NSString* sample in @[@"\u05D0", @"\u0627", @"\u3042", @"\u4E2D", @"\uAC00"]) {
        CTFontRef face = CTFontCreateForString(system, (__bridge CFStringRef)sample,
                                               CFRangeMake(0, static_cast<CFIndex>(sample.length)));
        if (face == nullptr) { continue; }
        if (const auto id = load_face(engine, face, size, seen)) { fallbacks.push_back(*id); }
        CFRelease(face);
    }
    CFRelease(system);
    need(engine.set_fallbacks(*primary, fallbacks));
    return *primary;
}

} // namespace studio
