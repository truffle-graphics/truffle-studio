# Handoff

## Current state

Foundation batch (#1) in progress on `feat/studio-foundation`:

- #2 scaffold: Truffle (`develop`) and SDL3 (`release-3.4.20`) submodules,
  CMake building Truffle GUI/GUI Dock/Renderer/Metal and static SDL3, and a
  placeholder `truffle-studio` executable. Strict build is warning-clean.

## Next

- #3 SDL3 host window rendering Truffle GUI with input and IME.
- #4 first docked workspace UI.

## Risks

- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
