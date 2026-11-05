# Handoff

## Current state

Milestone 1 (#6) is in progress on `feat/cad-sketch-workspace`. Studio is one
application with workspaces (CAD first) over a shared document; see ADR 0001.

- #7 product direction: PROJECT_CONTEXT, README and ADR 0001 record the
  workspace model, sketch-based CAD, OpenCASCADE behind `src/kernel`, and the
  Studio-owned constraint solver.
- #8 document model: `src/model` (no Truffle) has `UndoHistory<State>`
  (snapshot undo, grouped/nested edits, cancel) and `Document` (state,
  edit/begin/update/commit, undo/redo, change listeners, id allocation).
  The app owns one `Document`; toolbar Undo/Redo and Cmd+Z / Shift+Cmd+Z
  (ignored while a text field is focused) drive it. `tests/` holds a small
  harness; `ctest --preset strict` runs `studio_unit` and the smoke test.
- #9 workspaces: `studio/shell` owns the top bar (workspace switcher,
  Undo/Redo, Reset layout), the active workspace's dock and the status line.
  `studio/workspace.hpp` defines `Workspace` (panels built into a fresh
  dock on activation, torn down on switch, layout kept per workspace) and
  `EngineProfile` (clear colour, depth, continuous redraw; the app applies
  it on switch). `studio/cad/cad_workspace` is the real workspace (Model,
  Viewport, Inspector with part name); Animation, Simulation and Design are
  disabled `PlannedWorkspace` entries. The old Scene/Viewport/Properties
  placeholder UI is gone. Smoke registers a `Probe` workspace to test
  switching, profile changes and layout retention.
- #10 kernel: `external/occt` (V8_0_1, shallow) builds through
  `cmake/StudioOcct.cmake` as an ExternalProject (Release, shared, only
  TKPrim/TKMesh/TKBO and dependencies; about 2 minutes clean) into
  `build/<preset>/occt`. `src/kernel/kernel.hpp` is the OCCT-free interface
  (lines, arcs, circles, loops, profiles with holes, plane frames; extrude,
  tessellate, volume, bounds; `KernelError`). `occt_kernel.cpp` implements
  it, normalising loop winding and uniting multiple profiles. Seven kernel
  tests cover volumes, winding, tolerance, arcs/holes, planes, open-profile
  rejection and unions.

## Next

#11 sketch editor, #12 constraint solver, #13 3D viewport, #14
extrude and regeneration.

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index.
- During macOS live resize SDL blocks the event loop.
- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
