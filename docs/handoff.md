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
  drive it (a focused text field keeps them for its own text undo). `tests/` holds a small
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
- #11 sketching: `model/sketch.hpp` (sketches on XY/XZ/YZ with points,
  lines, CCW arcs and circles sharing endpoints by id) lives in
  `DocumentState::sketches`; `model/sketch_edit.hpp` adds sketches, shared
  points, curves, rectangles, removal with orphan cleanup and `find_loops`
  (simple closed loops; branching/dangling chains count as open).
  `studio/cad/sketch_editor` is the viewport canvas (Truffle `CanvasBuilder`):
  1-2-5 grid with coloured axes, right/middle-drag pan, wheel zoom about the
  cursor, snapping to points then grid, Select/Line (chained, closes on the
  first point)/Rectangle/Circle/Arc tools (buttons or S/L/R/C/A), Esc,
  Backspace/Delete. Each drawn shape is one undo step; drawing with no
  sketch creates one on XY. The Model panel lists sketches (rebuilt in
  `update()`, never during a click) with New sketch on XY/XZ/YZ; the
  Inspector shows curve, point and profile counts.
- #12 constraints: `Sketch::constraints` (coincident, horizontal, vertical,
  parallel, perpendicular, equal, distance, radius, diameter, fixed).
  `model/solver.hpp` solves free point coordinates and circle radii with
  Levenberg-Marquardt (numeric Jacobian; arcs keep end and start on one
  circle), reports DOF and redundancy from the Jacobian rank, and writes
  back only on convergence. `constrain()` rejects unsuitable entities,
  duplicates and anything over-constraining; `set_dimension()` re-solves
  or leaves the sketch alone. Rectangles get H/V constraints; removing
  entities drops their constraints. The editor has Shift-click
  multi-select, a constraint toolbar (Dimension = line length, point
  distance, circle diameter, arc radius), point dragging that re-solves
  with the point pinned (one undo step), and dimension text fields placed
  on the canvas in `update()` (Enter applies; events from fields are not
  canvas shortcuts). Curves turn green when fully constrained and red when
  over-constrained; the Inspector shows the status and DOF.
- #13 3D viewport: needs Truffle render targets (truffle#255, merged in
  #256; the submodule points at it). `studio/scene_view` renders the solid
  mesh offscreen (embedded MSL, depth, two-sided lighting) into a
  `SampledTexture::render_target` that the GUI shows as an element texture;
  the presenter adds the requested pass to the frame graph before the GUI
  pass with a shader-read barrier and a dependency. Studio now enables
  `TFL_SHADER_ENABLE_METAL_COMPILER` to compile that program at startup.
  `Workspace::before_frame()` runs after layout and before recording, so
  the pass matches the current element size. The CAD viewport has
  Sketch / 3D / Fit; `studio/cad/model_view` uses `cad/orbit_camera.hpp`
  (Z up; left drag orbits, right/middle drag pans, wheel zooms) and draws
  the axes and every sketch (active one highlighted) over the solid.
  `cad/sketch_geometry.hpp` holds the plane mapping (XZ: y→Z, YZ: x→Y,
  y→Z) and curve sampling shared with the editor. A hidden 3D page requests
  no passes. Unit tests cover the camera and plane mapping.
- #14 extrude and regeneration: `model::Extrude` (sketch, signed
  distance) lives in `DocumentState::extrudes` and shares the id sequence
  with sketches. `features/regenerate` (library `studio_features`, model +
  kernel only) turns each sketch's closed loops into profiles (a loop
  nested at odd depth is a hole of its innermost encloser; reversed arcs
  use `kernel::Arc::reversed`) and extrudes them; failures are recorded per
  feature and never thrown. Separate solids are meshed side by side (no
  boolean union yet). The Model panel is a feature tree in creation order;
  "Extrude sketch" extrudes the active sketch 10 mm, selects it and fits
  the 3D view. The workspace regenerates in `update()` only when extrudes
  or their sketches change; the Inspector edits the selected extrude's
  distance (Enter applies, focus returns to the view) and shows its volume
  or error. The smoke test drives extrude, width and distance edits and
  undo through the UI.

## Next

Milestone #6 complete: open the batch PR for `feat/cad-sketch-workspace`.
Then plan the next CAD milestone (booleans between features, sketch on a
face, save/load).

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index.
- During macOS live resize SDL blocks the event loop.
- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
