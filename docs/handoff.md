# Handoff

## Windows bring-up (#22): build and dependency foundation

The owner approved Windows parity with existing macOS Studio functionality,
Vulkan on Windows, and signed commits pushed between verified phases.
`feature/windows-studio` builds on the pending CAD UI cleanup, preserving it.
The Truffle pin advances to verified develop merge `0a1d93d`.

PowerShell now initializes the existing MSVC x64 developer environment before
Ninja discovery, checks tools, and prepares only pinned required submodules.
Check mode performs no fetch/install; dirty submodules are not moved.
OCCT builds Release DLLs in `out/debug/bin` and import libraries in
`out/debug/lib`, keeping its existing isolated headers/build staging.
All existing document, kernel, sketch, solver, camera and feature unit tests
execute successfully on Windows with strict first-party MSVC warnings.

The owner chose `TFLST_` for Studio-owned configuration (replacing `STUDIO_`);
Truffle's `TFL_` options are unchanged. Build names and script warning toggles
remain the same. Windows application execution is not claimed by this phase:
the native GUI smoke exposed Truffle's Metal-only GUI programs. A separate
Truffle session owns Vulkan GUI program support; Studio host/shader/font and
end-to-end CAD parity validation follow. macOS was not rerun on Windows.

## Current state

Usability-first CAD cleanup (#21) is approved for commit and push on
`feature/cad-ui-cleanup`; merging remains pending. The Truffle submodule now
points at `b4ba538` (the merged #261 themes);
the previous pin was still `3494345`, predating the neutral themes.

The shell drops the duplicate app title, hides its switcher with only one
available workspace, uses neutral theme bindings and moves Reset layout to
the status bar. Studio's UI construction uses compact content-sized controls;
Undo/Redo follow history availability. Model/Viewport/Inspector default to
18/64/18 proportions. All plane buttons share a separate row; features scroll
and Extrude requires a closed profile. The Inspector scrolls, shows selected
feature details, and lays out distance labels/fields vertically. Sketch tools
stay above the canvas; constraints appear in the Inspector only with selected
sketch geometry, use the model's suitability checks, and reflow between one
and two columns. Fit is available only in 3D; entering sketch mode clears the
extrude selection. Ordinary selection highlights use Truffle theme tokens;
geometry's semantic constraint/axis colours remain application-owned.

The Debug build with warnings-as-errors and unit/native smoke tests pass.
Smoke coverage now checks sole/multiple-workspace chrome, plane/toolbar label
containment at 1280x800 and 1024x700, canvas space, history and extrusion
availability, and contextual constraints. Native visual inspection confirms
the compact neutral shell; no custom Studio theme was added.

Pre-release versioning policy (user decision) is recorded in
`PROJECT_CONTEXT.md`: until the first release tag, breaking changes within
Studio, within Truffle or between them need no version bump or compatibility
path.

User-approved tooling work (#18) adds native setup/build entry points,
debug/release/relwithdebinfo presets, final executables in `out/<preset>/bin`, libraries
in `out/<preset>/lib` and dependency headers/staging under
`install/<preset>/dependencies`. Logs/probes belong in `build/_scratch`.
Setup confirms missing-tool installation and prepares only the pinned Studio
and required GUI-text submodules. Dirty submodule checkouts are not moved.
Studio remains Apple Silicon/macOS-only; PowerShell entry points fail explicitly.
Warnings-as-errors is independently toggled with `--warnings-as-errors on|off`,
which sets the policy for first-party Studio and Truffle targets in the same
build tree. New builds default to off; each configuration remembers its setting.
The revision passes 15 isolated script checks, all three normal configurations
and targeted Base builds that confirm cache persistence and shared output
paths. Actual Studio and nested Truffle compiler flags reject a warning with
on and permit it with off. The full app/smoke run was not repeated for this
configuration-only revision; existing generated dev/strict artifacts remain
untouched.
No install script or workspace installer is implemented. Before the naming revision, the strict build,
unit/smoke tests and OCCT lookup through `@loader_path/../lib` pass on macOS;
the isolated shell entry-point safety/error tests also pass.

Milestone 1 (#6) is complete and merged via PR #15. CAD is the only
implemented workspace. Optional installation of future workspaces and data
sharing between them are planned, not implemented; see ADR 0001.

- #7 product direction: PROJECT_CONTEXT, README and ADR 0001 record the
  workspace model, sketch-based CAD, OpenCASCADE behind `src/kernel`, and the
  Studio-owned constraint solver.
- #8 document model: `src/model` (no Truffle) has `UndoHistory<State>`
  (snapshot undo, grouped/nested edits, cancel) and `Document` (state,
  edit/begin/update/commit, undo/redo, change listeners, id allocation).
  The app owns one `Document`; toolbar Undo/Redo and Cmd+Z / Shift+Cmd+Z
  drive it (a focused text field keeps them for its own text undo). `tests/` holds a small
  harness; `ctest --preset debug` runs `studio_unit` and the smoke test.
- #9 workspaces: `studio/shell` owns the top bar (conditional workspace switcher,
  Undo/Redo), the active workspace's dock and the status line with Reset layout.
  `studio/workspace.hpp` defines `Workspace` (panels built into a fresh
  dock on activation, torn down on switch, layout kept per workspace) and
  `EngineProfile` (clear colour, depth, continuous redraw; the app applies
  the clear colour on switch). `studio/cad/cad_workspace` is the CAD
  workspace (Model, Viewport, Inspector with part name); the normal app
  registers no other workspaces. Smoke registers an unavailable workspace
  and a `Probe` workspace to test disabled entries, switching, profile
  selection and layout retention.
- #10 kernel: `external/occt` (V8_0_1, shallow) builds through
  `cmake/StudioOcct.cmake` as an ExternalProject (Release, shared, only
  TKPrim/TKMesh/TKBO and dependencies; about 2 minutes clean) into
  `out/<preset>/lib`, with headers/staging under
  `install/<preset>/dependencies/occt` and intermediate compilation under
  `build/<preset>/occt-build`. `src/kernel/kernel.hpp` is the OCCT-free interface
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
  multi-select, Inspector constraint commands (Dimension = line length, point
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
  use `kernel::Arc::reversed`; crossing loops fail the feature) and
  extrudes them; failures are recorded per
  feature and never thrown. Separate solids are meshed side by side (no
  boolean union yet). The Model panel is a feature tree in creation order;
  "Extrude sketch" extrudes the active sketch 10 mm, selects it and fits
  the 3D view. The workspace regenerates in `update()` only when extrudes
  or their sketches change; the Inspector edits the selected extrude's
  distance (Enter applies, focus returns to the view) and shows its volume
  or error. The smoke test drives extrude, width and distance edits and
  undo through the UI.

## Next

Plan the next CAD milestone (booleans between features, sketch on a face,
save/load). Design optional workspace installation and cross-workspace data
sharing separately when needed.

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index.
- During macOS live resize SDL blocks the event loop.
- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
