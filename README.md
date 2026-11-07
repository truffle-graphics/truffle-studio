# Truffle Studio

Truffle Studio is a multi-purpose authoring application built on
[Truffle](https://github.com/truffle-graphics/truffle). CAD is the only
implemented workspace, focused on sketch-based parametric part modelling.
Future workspaces are intended to be optional and independently useful, with
data sharing between them as a future goal. Workspace installation and
integration mechanisms are not yet decided or implemented.

Studio is Truffle's dogfooding application: features Studio needs drive Truffle
work, which is tracked as Truffle issues. Truffle stays a library; Studio is
the application.

Status: early foundation. macOS (Apple Silicon) with Metal; Windows x64
Vulkan bring-up is tracked by #22. The Windows build/dependency foundation
is verified; native GUI/CAD parity verification is in progress.

## Layout

Clone Studio beside the Truffle checkout:

```text
Truffle/
├── truffle/          # Truffle development checkout
└── truffle-studio/   # this repository
    └── external/
        ├── truffle/  # Truffle submodule (tracks develop)
        ├── SDL/      # SDL3 submodule (pinned release)
        └── occt/     # OpenCASCADE submodule (pinned release, shallow)
```

## Setup

```sh
git clone https://github.com/truffle-graphics/truffle-studio.git
cd truffle-studio
./setup.sh
```

Setup checks Git, CMake 3.23+, Ninja and Xcode command-line tools, and prepares
the pinned Studio submodules and only the Truffle vendor submodules needed for
GUI text. It does not use `--recursive` or fetch optional shader toolchains.
Missing tools are installed only after confirmation through existing Homebrew
or Apple's command-line-tools installer. Homebrew itself is not bootstrapped.
Finish any graphical installer before rerunning setup. `./setup.sh --check`
only verifies prerequisites: it performs no installation or fetching. Dirty
submodule checkouts are never moved.

## Build and run

```sh
./build.sh
./out/debug/bin/truffle-studio
ctest --preset debug
```

`./build.sh release` selects an optimized build; `./build.sh relwithdebinfo`
selects an optimized build with debugging information. Warnings-as-errors is
a separate toggle within any selected configuration:

```sh
./build.sh debug --warnings-as-errors on --parallel 4
./build.sh debug --warnings-as-errors off --parallel 4
```

New builds default to off. Each configuration remembers its setting until
explicitly changed; toggling does not create a different build directory.
The flag applies to first-party Studio and Truffle targets, not vendored
third-party warning policy. Direct CMake use can set
`-DTFLST_WARNINGS_AS_ERRORS=ON` or `OFF` with the same effect.
Other build arguments are passed to `cmake --build`. Scripts locate the repository
independently of the current directory; direct CMake commands remain supported.
On Windows, use `.\setup.ps1 -Check` to verify prerequisites or `.\setup.ps1`
to prepare pinned dependencies, then
`.\build.ps1 debug -WarningsAsErrors on --parallel 4`.
Visual Studio C++ Build Tools and the Windows SDK are required; setup activates
their x64 developer environment before looking for Ninja. Missing Git/CMake/Ninja
installation through winget requires confirmation. The Windows application
phase follows the build foundation; core-only checks can use
`-DTFLST_BUILD_APP=OFF`. No install script is provided yet.

```text
build/<preset>/         CMake internals and intermediate dependency builds
out/<preset>/bin/       truffle-studio and test executables
out/<preset>/lib/       Studio, Truffle, SDL and OCCT libraries
install/<preset>/dependencies/  dependency headers and build-time staging
build/_scratch/         temporary validation logs and probes
```

All these directories are ignored by Git. `TFLST_OUTPUT_DIR` and
`TFLST_DEPENDENCY_PREFIX` can override the direct-build locations.

The first build also compiles the OpenCASCADE modelling toolkits (Release,
shared libraries). Their intermediate build stays in `build/<preset>/occt-build`;
the usable libraries go to `out/<preset>/lib` and headers/staging to
`install/<preset>/dependencies/occt`. This takes a few minutes on
Apple Silicon. Later builds reuse it.

## Updating Truffle

```sh
git -C external/truffle fetch origin develop
git -C external/truffle checkout origin/develop
git add external/truffle
```

## CAD workspace

The Model panel creates sketches on XY, XZ or YZ and lists the document's
features. Drawing directly in an empty sketch view starts an XY sketch.
Extrude becomes available once the active sketch has a closed profile.
Sketch/3D switches the viewport; Fit operates in the 3D view.

Drawing tools stay above the sketch canvas. Select geometry to reveal
constraints in the Inspector; only commands suitable for the selection are
enabled. Constraints reflow to one column in a narrow Inspector. The Inspector
also shows sketch/feature details and the selected extrude's editable distance.
Both side panels support scrolling.

Undo/Redo follow document history. Reset layout is at the right of the status
bar. Workspace switching appears only when more than one implemented workspace
is registered; the normal application currently opens only CAD.

## Documentation

- `PROJECT_CONTEXT.md`: purpose and boundaries
- `AGENTS.md`: contribution and delivery rules
- `docs/handoff.md`: current state and next steps

## License

Apache License 2.0. See `LICENSE`.

Studio links OpenCASCADE dynamically, under the LGPL-2.1 with the
OpenCASCADE exception (see `external/occt/LICENSE_LGPL_21.txt` and
`external/occt/OCCT_LGPL_EXCEPTION.txt`).
