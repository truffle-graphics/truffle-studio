# Handoff

## GUI-owned Studio application (#26)

Studio now builds only the SDL host and a GUI-owned `gui::Application`,
linking `Truffle::GUI` rather than Renderer, Presentation, dock, backend,
font or scene targets. SDL still owns the native window, input and event
loop; Studio passes an HWND/HINSTANCE pair on Windows or SDL's CAMetalLayer
on macOS, with pixel size and DPI. The GUI root remains empty and transparent,
and the application clear retains the opaque dark client background. Legacy
shell, CAD and scene files remain in the tree but are not part of the app.

The Truffle submodule is pinned to
`99a585723cb7a4f944d5eb817cc5bc786ef03830` on `feat/gui-menus`, including
the consumer APIs (#274-#277). The smoke keeps scheduling, blank GUI batch,
inert input, minimize/restore, resize and close routing. `out_of_date` and
`suboptimal` are recoverable: a frame reported as presented is counted even
when the status is suboptimal, and a follow-up frame lets Truffle rebuild its
presentation resources. The host skips drawing and ignores pending GUI frame
requests while the SDL window has no drawable area, waiting for window events
instead of spinning while minimized.

The GUI-owned API does not expose the presented swapchain image, but the
previous pixel smoke did not read it either. Its equivalent offscreen clear
and BGRA readback is restored in `smoke_readback.cpp`, isolated from normal
GUI setup and using the application's device through explicit Renderer/RHI
interop. The application passes the same clear color; the check verifies
uniform opaque BGRA 15,13,13,255 at the tested sizes.

Smoke sets `enable_validation` for native Presentation. The inspected Vulkan
backend conditionally enables `VK_LAYER_KHRONOS_validation` when installed,
but does not create a debug messenger or use the RHI `InstanceDesc` callback.
The GUI API cannot supply that callback, so there is no Studio diagnostic
callback coverage and no claim that validation messages are captured; the
CTest VUID failure match only detects output if some layer or runtime emits it.

Only the smoke's frame/reporting checks run through GUI public types. The
native presentation internals and graphics device are not reached from normal
Studio code. A device-loss result is surfaced as a fatal restart-required
error; surface loss attempts `replace_window` against the still-live SDL
window. The GUI application owns one Context/GUI stream; independently paced
detached windows sharing a context remain an open Truffle design question
(#276), not addressed by Studio.

Validation is intentionally limited: `app.cpp`, `sdl_window.cpp`,
`host_tests.cpp` and the smoke-only `smoke_readback.cpp` compile against the
pinned headers. No executable link, test suite, native smoke or Metal/Vulkan
pixel test is claimed.

## Owner-approved GUI and CAD roadmap (#25)

The approved sequence in [Studio #25](https://github.com/truffle-graphics/truffle-studio/issues/25)
is to build the GUI-only application shell first (menus, panels, controls and
docking), keeping the CAD viewport an empty placeholder. Next, develop the
Truffle Engine (#272) together with the CAD viewport and connect the GUI
controls to the Engine. There is no interim Renderer-based viewport. The
independently paced shared-context window API remains an open Truffle design
dependency (#276) for future multi-window docking needs; Studio does not
design around or implement it in advance.

## Earlier Truffle presentation integration (#24)

Studio now uses Truffle's optional `Truffle::RendererPresentation` API for
surface/swapchain setup, resize/recreation, zero-size suspension and frame
pacing. SDL remains the owner of the native window and event loop; Studio
borrows its existing Vulkan-validated (Windows) or Metal (macOS) device and
keeps the clear-only pass, blank pixel readback, lifecycle and diagnostics
smoke checks. No GUI, viewport, menu or CAD scene is constructed.

The Truffle submodule is pinned to signed/pushed `79aad7a09e306bc8eed8588fed381225fedf3308`
on `feat/gui-menus`, which includes presentation support (#270) and the
viewport API (#271). Studio only uses the presentation API. Validation for
this integration is intentionally limited: the Studio app translation unit
compiles with the existing Windows Debug flags against the pinned headers.
The executable was not linked, and no full build, test suite, or native
Vulkan/Metal pixel smoke is claimed.

## Blank Studio client for UI redesign (#23)

The owner requested a genuinely blank client background: no panels,
toolbars, menus, buttons, workspace switcher, inspector/status controls,
sketch grid/geometry or scene. Native OS window/title-bar controls remain.
Startup now constructs only the native host, a clear-only presenter and
the document. It does not construct a GUI context, load fonts, activate a
workspace or translate editing input, so old widgets are not merely hidden.

Document/history, sketch/constraint/solver, feature regeneration, kernel
and camera/geometry logic and their unit tests remain unchanged. Legacy
shell/workspace/editor/scene source is retained for deliberate reuse.
Truffle menus are separate concurrent work and are not integrated here.

The app smoke now checks a single presented clear pass with zero draw or
compute batches, exact opaque BGRA background pixels at several sizes,
idle/minimize/restore/resize behavior, inert former UI input with real
document history and owner-window close routing.

All 13 Studio-owned app/host translation units compile under MSVC Debug
with `/WX`; the standard executable and host tests link successfully.
The three relevant CTests pass: blank native smoke, all 46 retained core
unit cases, and Win32 host/font checks. Native render-target readback is
uniform BGRA 15,13,13,255 at 1280x800, 1024x700 and 1600x900; no graphics
diagnostics are emitted with Vulkan validation requested. A screen capture
of the normal 1280x800 client independently shows the same blank interior;
only 36 pixels at Windows' rounded bottom corners are native compositor
decoration. There is no application menu or native child control, and the
normal window responds and exits successfully through its native close.

Validation used the existing Debug dependencies and a coordinated coherent,
in-progress Truffle menu header/library snapshot (rebuilt GUI/dock archives
from `external/truffle/out/debug/lib`). No Truffle configuration, build,
source change or gitlink staging was performed by this work. The Studio
compile/link recipes were executed directly to avoid scheduling dependency
rebuilds. This is Windows RTX 4080 SUPER evidence, not a full clean build or
macOS rerun. The shared menu work is not claimed as delivered or integrated.

Delivery is one signed issue-scoped commit, pushed immediately on
`feature/windows-studio`. No PR, merge, branch cleanup or version bump is
authorized.

The sections below are historical delivery evidence. Their former visible
CAD workflows are not current product behavior after this blank reset.

## Windows native host and CAD/GUI parity (#22)

Windows uses SDL's borrowed HWND with the public Vulkan surface contract;
macOS retains its SDL Metal view and Cocoa-layer path. Studio compiles its
own GLSL/SPIR-V scene stages on Windows and MSL on macOS. Windows clip Y is
adapted in the scene vertex shader while the shared camera keeps [0,1] depth.
System UI fonts are selected through Win32 GDI and handed to Truffle as font
bytes, with ordered script fallbacks. Latin/Hebrew/Arabic/Japanese shaping
tests have no missing glyphs on this machine. Clipboard/cursor/IME requests
remain SDL-owned; Control shortcuts replace Command on Windows.

All 46 existing non-GUI tests and the full native CAD/GUI smoke pass locally:
workspace/history, dock save/restore, labels at several window sizes, IME
composition/commit, sketch entities and selection, constraints/dimensions,
extrusion and regeneration, sampled 3D view and orbit/pan/zoom. Native target
readback verifies opaque output, exact clear background and visible solid
coverage, rather than relying on a submitted pass count. The Windows host
fixture also exercises clipboard, composition byte offsets and owner-window
event routing. Minimize suspends rendering without a pending-frame busy loop;
restore/expose events request redraw.

Strict MSVC Debug build and all four CTest entries pass (unit, native host,
full CAD smoke and nine isolated PowerShell safety tests). Windows evidence
is RTX 4080 SUPER with Vulkan validation requested, no native diagnostics.
The reusable Vulkan GUI prerequisite is signed and pushed as `07331517`
on Truffle's `feature/gui-vulkan-painter` (#268). Studio pins that revision:
embedded prepared SPIR-V adds no runtime compiler dependency to GUI, while
Studio's scene still uses its selected GLSL compiler. Its seven focused
native checks and compiler-disabled GUI tests passed independently.
No PR/merge, hosted work, version bump or macOS rerun is implied.

## Windows regression coverage and wrapper closeout (#22)

Follow-up: PowerShell's implicit positional binding consumed `--parallel`
as `WarningsAsErrors` when that option was omitted. Binding is now explicit:
the preset and forwarded arguments are positional, warning policy is
named-only. All ten isolated fixtures and the exact
`.\build.ps1 debug --parallel 4` invocation pass.

Native host/font tests and nine isolated PowerShell fixture cases are now
part of CTest. Fixtures run from outside their temporary repository root,
exercise help without tools, preserve build arguments and cached warning
policy, propagate configure/build failures, and enforce read-only check mode,
dirty-checkout preservation and named-only pinned dependency preparation.
Python is optional for tooling fixtures; native/unit smoke remains available
without it. The build wrapper accepts help through either PowerShell binding.

The integration phase is signed and pushed as `2a34ad8`, following build
foundation `5ce1219`. The final pinned setup check and strict Debug build
pass; all four CTest entries pass against Truffle `07331517`. Windows app
builds by default in new configurations; existing explicit app-off caches
remain respected. Issue #22 is ready for integration review, not merged.

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

## Prior CAD/UI and tooling delivery

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
At that revision Studio was Apple Silicon/macOS-only and PowerShell entry
points failed explicitly; the Windows phases above supersede that limitation.
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

Design Studio's replacement presentation from the blank background before
reconnecting controls or integrating menus. Preserved CAD logic can be
reused behind that design. Later CAD milestones (booleans between features,
sketch on a face, save/load), optional workspace installation and data
sharing remain separate planning work.

## Risks

- Font collections (`.ttc`) load only their first face because Truffle's
  `TextEngine::load_font` has no face index.
- During macOS live resize SDL blocks the event loop.
- SDL3 IME composition ranges are character-based; the host converts them
  to Truffle's UTF-8 byte ranges.
