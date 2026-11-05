# ADR 0001: One workspace-based authoring application, CAD first

Status: Accepted (#6, #7)

## Context

The owner wants CAD, animation, simulation and design tools. These share most
of their foundations: documents, undo, selection, properties, viewports and
file handling. Building them as separate applications would duplicate that
work and stop work moving between them.

## Decision

- **One application, several workspaces.** Studio is a single application.
  A workspace (CAD, Animation, Simulation, Design) is a mode over one shared
  document model. It supplies its own panels, dock layout, tools and an
  **engine profile**: the rendering and runtime configuration it is optimised
  for. Switching workspace never discards document state.
- **CAD first, sketch-based.** The first complete workspace is parametric
  part modelling in the style of Fusion 360 and SolidWorks: constrained 2D
  sketches on a plane, then features such as extrude (later revolve, fillet
  and booleans) that regenerate when parameters change.
- **Geometry kernel: OpenCASCADE behind a Studio interface.** Exact solid
  geometry (boundary representation, or B-rep) uses OpenCASCADE. It is a Git
  submodule pinned to a release tag (`V8_0_1`) and built from source with
  only the modelling toolkits. It is linked as shared libraries to satisfy
  its LGPL-2.1 licence (which carries an exception). No OpenCASCADE type
  appears outside `src/kernel`, so the kernel stays replaceable.
- **Studio-owned 2D constraint solver.** Sketch constraints are solved by a
  small numerical solver in Studio (damped least squares). OpenCASCADE does
  not provide one, and the comparable solvers are either GPL or tied to their
  host applications.
- **Truffle remains a library.** Workspaces, documents, the kernel and the
  solver are Studio concerns. Studio files reusable rendering or GUI needs as
  Truffle issues.

## Consequences

- The first clean build is slow, because OpenCASCADE compiles from source;
  incremental builds are unaffected.
- Studio ships OpenCASCADE shared libraries next to the executable.
- The animation, simulation and design workspaces appear in the switcher
  only as unavailable entries until each is planned.
