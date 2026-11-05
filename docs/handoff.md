# Handoff

## Current state

Milestone 1 (#6) is in progress on `feat/cad-sketch-workspace`. Studio is one
application with workspaces (CAD first) over a shared document; see ADR 0001.

- #7 product direction: PROJECT_CONTEXT, README and ADR 0001 record the
  workspace model, sketch-based CAD, OpenCASCADE behind `src/kernel`, and the
  Studio-owned constraint solver.

## Next

#8 document model and undo, #9 workspace switcher, #10 OpenCASCADE and the
kernel, #11 sketch editor, #12 constraint solver, #13 3D viewport, #14
extrude and regeneration.

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index.
- During macOS live resize SDL blocks the event loop.
- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
