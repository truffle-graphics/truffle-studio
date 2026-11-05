# Handoff

## Current state

Foundation batch (#1) in progress on `feat/studio-foundation`:

- #2 scaffold: Truffle (`develop`) and SDL3 (`release-3.4.20`) submodules,
  CMake building Truffle GUI/GUI Dock/Renderer/Metal and static SDL3, and a
  placeholder `truffle-studio` executable. Strict build is warning-clean.
- #3 SDL3 host: `src/host/sdl_window` (Metal layer, pointer/wheel/key/text/
  IME composition/focus translation, cursor, clipboard and text-input
  services), `src/host/system_font.mm` (CoreText font discovery with
  best-effort script fallbacks), `src/studio/app` (Truffle Metal presentation,
  idle-waiting loop, `--smoke` script). `ctest` smoke passes under Metal
  validation: idle draws nothing, injected SDL clicks and IME
  composition/commit reach widgets.

## Next

- #4 first docked workspace UI.

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index; some CJK fallbacks may be
  incomplete. Candidate Truffle issue if it shows up in use.
- During macOS live resize SDL blocks the event loop, so content may stretch
  until the drag ends.

- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
