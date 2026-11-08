# Project Context

## Purpose

Truffle Studio is one multi-purpose authoring application built on Truffle.
It replaces throwaway editor-style samples as the place where Truffle is
exercised under real use.

- **Workspace model.** Studio is one application with independently useful
  workspaces. CAD is the only implemented workspace and the focus for the
  foreseeable future. A future 3D content-creation workspace, including
  animation and rendering, has not yet been named.
- **Optional workspaces.** Users should be able to install only the workspaces
  they want and add others later. Packaging, installation and loading are
  undecided and not implemented. Sharing data between installed workspaces is
  a future goal, not a required workflow.
- **CAD.** Sketch-based parametric part modelling uses constrained 2D sketches
  and features such as extrude.
- **Geometry.** OpenCASCADE (a source submodule) sits behind the
  Studio-owned interface in `src/kernel`. Sketch constraints use a
  Studio-owned solver.

See [ADR 0001](docs/adr/0001-workspace-application.md).

## Current presentation

The owner requested a blank client background while Studio's UI is
redesigned (#23). The first GUI-only shell increment (#28) adds an in-client
Truffle menu bar with File > Quit and Edit > Undo / Redo. The remainder is
empty: no workspace, panels, sketch canvas or 3D scene. Startup constructs
the SDL host, a separate `gui::Context`, optional `gui::Graphics`, a system
font and the document. Truffle owns the graphics device and presentation;
SDL retains native window/event ownership and lifecycle.

Studio lets Truffle determine the menu bar, trigger, popup and item dimensions
and padding; the root layout stretches the bar across the client. Studio
selects the system UI font and uses Truffle's default font size. Menu actions
and platform shortcuts use the same commands; history commands reflect
availability and do not steal a focused text editor's shortcuts.

The CAD/workspace implementation and independent document, solver, feature,
kernel and camera tests are preserved for reuse, not hidden interactive UI.
The workspace model above remains the product direction, not a description
of currently available controls.

## GUI defaults during development

Use Truffle's defaults for all Studio GUI components, including their sizing,
styling and behavior, so the owner can evaluate those defaults. Do not
override available defaults in Studio unless the owner explicitly directs a
change and this project context is updated to record it. Until both happen,
the default-first decision remains in force. Compose application layouts as
needed and supply explicit values where Truffle provides no default.

## Approved UI and CAD roadmap (#25)

Build the GUI-only application shell first: menus, panels, controls and
docking, with the CAD viewport remaining an empty placeholder. Then develop
the Truffle Engine (#272) together with the CAD viewport and connect the GUI
controls to that Engine. Do not build an interim Renderer-based viewport.

## Boundaries

- Studio owns windowing (SDL3), the event loop, fonts, documents, Context and
  all application state. Truffle's optional GUI Graphics API provides normal
  presentation and owns the graphics consumers. Renderer and RHI are not used
  directly by the normal Studio app.
- Studio uses Truffle only through public GUI headers and CMake targets for
  normal application work. Specialist access belongs only in narrowly scoped
  diagnostics or test code when the public consumer API lacks a capability.
- When Studio needs something reusable from Truffle, file a Truffle issue and
  implement it there. Do not build Truffle-shaped workarounds inside Studio,
  and do not push application concepts into Truffle.
- Truffle is a Git submodule at `external/truffle` tracking `develop`; SDL3
  and OpenCASCADE are submodules pinned to release tags.
- Document, workspace, sketch and solver code does not depend on Truffle or
  OpenCASCADE. Only the kernel implementation uses OpenCASCADE types.

## Versioning before the first release

Studio and Truffle are developed together; Studio is how Truffle is proven,
not a conventional consumer. Until the first release tag (a prerelease such as
`0.1.0` counts), breaking changes within Studio, within Truffle, or between
them need no version bump, compatibility shim or deprecation path; callers
are updated in the same change. From the first release tag onwards, breaking
and patch changes are versioned and Studio adapts to Truffle releases.

## Platform

macOS on Apple Silicon with Metal, and Windows x64 with Vulkan. Other
platforms follow when Truffle's backends and Studio's host code are verified.

Windows x64 bring-up (#22) uses Vulkan, selected by the owner. Native
PowerShell setup/build, OCCT/kernel, system font fallback, SDL Win32 host
input/clipboard/IME and full CAD/GUI smoke are verified with strict MSVC.
Truffle's Vulkan GUI programs remain owned by Truffle; Studio owns its
Windows host, GLSL scene sources and font policy. The sampled 3D viewport
has native readback evidence; existing sketch/constraint/extrude/history,
layout and orbit/pan/zoom smoke is shared with macOS. Native resize and
minimize/restore also passed for that former presentation. Windows graphics
evidence is the local RTX 4080 SUPER run, not software-driver/ARM64/physical-
reset coverage. The Context/Graphics migration (#27) and menu integration
(#28) build and pass the native smoke and unit tests on macOS; Windows native
validation for these changes remains pending. Smoke can request validation,
but Truffle's Vulkan backend does not create a debug messenger or use the
custom diagnostics callback; the GUI API cannot provide that callback. The
presented swapchain image is not exposed for readback; smoke-only explicit
interop reads back a separate offscreen GUI target. See `docs/handoff.md`.
Studio-owned build options and configuration use the `TFLST_` prefix;
Truffle-owned options retain `TFL_`. No compatibility aliases are added
before the first release.

Native setup/build entry points use debug/release/relwithdebinfo CMake presets (#18/#22).
Final executables/libraries go to `out/<preset>/bin` and `lib`; OCCT
headers/staging go to `install/<preset>/dependencies`, while its private build
stays in `build/<preset>/occt-build`. Temporary logs/probes use `build/_scratch`.
Setup confirms missing-tool installation and prepares only pinned required
submodules. No install script or workspace installer is implemented.
Warnings-as-errors is an independent on/off switch within the chosen
configuration. It governs first-party Studio and Truffle targets, not vendored
third-party sources; each build remembers its setting.

## Planning

Work is tracked as issues in this repository and on the
[Truffle delivery project](https://github.com/orgs/truffle-graphics/projects/1).
