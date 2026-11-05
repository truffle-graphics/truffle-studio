# Project Context

## Purpose

Truffle Studio is one multi-purpose authoring application built on Truffle.
It replaces throwaway editor-style samples as the place where Truffle is
exercised under real use.

- **Workspaces over one document.** CAD, Animation, Simulation and Design are
  workspaces in the same application and share one document model. Each
  workspace supplies its own panels, tools and engine profile.
- **CAD first.** The first complete workspace is sketch-based parametric part
  modelling: constrained 2D sketches, then features such as extrude.
- **Geometry.** OpenCASCADE (a source submodule) sits behind the
  Studio-owned interface in `src/kernel`. Sketch constraints use a
  Studio-owned solver.

See [ADR 0001](docs/adr/0001-workspace-application.md).

## Boundaries

- Studio owns windowing (SDL3), the event loop, fonts, documents and all
  application state. Truffle provides graphics, rendering and GUI libraries.
- Studio uses Truffle only through public headers and CMake targets.
- When Studio needs something reusable from Truffle, file a Truffle issue and
  implement it there. Do not build Truffle-shaped workarounds inside Studio,
  and do not push application concepts into Truffle.
- Truffle is a Git submodule at `external/truffle` tracking `develop`; SDL3
  and OpenCASCADE are submodules pinned to release tags.
- Document, workspace, sketch and solver code does not depend on Truffle or
  OpenCASCADE. Only the kernel implementation uses OpenCASCADE types.

## Platform

macOS on Apple Silicon with the Metal backend. Other platforms follow when
Truffle's backends and Studio's host code are verified there.

## Planning

Work is tracked as issues in this repository and on the
[Truffle delivery project](https://github.com/orgs/truffle-graphics/projects/1).
