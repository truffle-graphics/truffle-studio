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
redesigned (#23). Startup constructs only the SDL host, a GUI-owned
`gui::Application` and the document: no shell, workspace, panels, widgets,
sketch canvas, fonts or 3D scene. The GUI root is empty and transparent; the
application owns its graphics device and paints only the dark client clear.
Application editing input is deliberately inactive; SDL retains native
window/event ownership and lifecycle.

The CAD/workspace implementation and independent document, solver, feature,
kernel and camera tests are preserved for reuse, not hidden interactive UI.
The workspace model above remains the product direction, not a description
of currently available controls. Truffle's independently developed menus
are not integrated in this reset.

## Approved UI and CAD roadmap (#25)

Build the GUI-only application shell first: menus, panels, controls and
docking, with the CAD viewport remaining an empty placeholder. Then develop
the Truffle Engine (#272) together with the CAD viewport and connect the GUI
controls to that Engine. Do not build an interim Renderer-based viewport.

## Boundaries

- Studio owns windowing (SDL3), the event loop, fonts, documents and all
  application state. Truffle's GUI-owned application API provides normal
  application presentation and owns the graphics consumers. Renderer and RHI
  are not used directly by the normal Studio app.
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
minimize/restore also pass. That CAD/GUI evidence describes the former
presentation; current app smoke checks the blank clear-only client and
native lifecycle instead. Windows
graphics evidence is the local RTX 4080 SUPER run, not
software-driver/ARM64/physical-reset coverage. macOS
Metal paths are preserved but were not rerun from this Windows machine.
The GUI-owned migration (#26) has not yet been verified on either native
backend. Its application API exposes validation enablement but not a custom
Vulkan diagnostics callback, and does not expose the presented image for
pixel readback; see `docs/handoff.md`.
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
