# ADR 0001: One workspace-based authoring application, CAD first

Status: Accepted (#6, #7; revised by #16)

## Context

The owner wants CAD first, with scope for other capabilities such as 3D
content creation, animation, rendering and simulation later. How these
capabilities are grouped into workspaces remains open. People may want to use
just one workspace or share content between several. Common foundations
include documents, undo, selection, properties, viewports and file handling.

## Decision

- **One application, optional workspaces.** Studio is a single application.
  A workspace supplies its own panels, dock layout and tools and should be
  useful on its own. CAD is the only implemented workspace. The future 3D
  content-creation workspace has no confirmed name. Users should be able to
  select workspaces at installation and add them later, but packaging, loading
  and whether installation requires a restart are undecided and not
  implemented.
- **Shared foundations and interoperability.** The current shell owns one
  document and preserves its state when switching workspaces. Sharing content
  between installed workspaces is a goal, but the data model and integration
  semantics for future workspaces remain to be designed.
- **Workspace configuration.** Studio workspaces declare a local
  `EngineProfile`; the app currently applies its background colour when
  switching. Workspace-specific Truffle engine configurations are a future
  possibility, not an implemented feature.
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
- Only CAD appears in the normal workspace switcher. The smoke test uses
  temporary workspaces to exercise switching and unavailable entries.
- Optional installation and cross-workspace data sharing require future
  design and implementation.
