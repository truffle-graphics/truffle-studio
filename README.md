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

Status: early foundation, with a deliberately blank client background while
Studio's UI is redesigned (#23). There are no application panels, buttons,
menus, sketch grid/geometry or active editing shortcuts; the native window
and title bar remain. CAD/document/kernel logic is retained for reuse, not
exposed through hidden widgets.

macOS (Apple Silicon) uses Metal and Windows x64 uses Vulkan. The former
Windows CAD/GUI presentation was verified under #22; current smoke checks
the blank background and native window lifecycle. This is not an ARM64,
software-driver or physical-GPU-reset claim.

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
Run `.\out\debug\bin\truffle-studio.exe`, or verify with `ctest --preset debug`.
Visual Studio C++ Build Tools and the Windows SDK are required; setup activates
their x64 developer environment before looking for Ninja. Missing Git/CMake/Ninja
installation through winget requires confirmation. Native graphics verification
uses the Vulkan loader/driver and Khronos validation layer; install the Vulkan SDK
for validation-enabled smoke runs. Core-only checks can use
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
Apple Silicon. Later builds reuse it. On Windows, OCCT Release DLLs go beside
the executable in `out/<preset>/bin`; import libraries remain in
`out/<preset>/lib`. No machine-global DLL search-path changes are required.

## Updating Truffle

```sh
git -C external/truffle fetch origin develop
git -C external/truffle checkout origin/develop
git add external/truffle
```

## Preserved CAD foundation

The document/history model, XY/XZ/YZ sketches, constraint solver, feature
regeneration, OpenCASCADE kernel and camera/geometry helpers remain in the
source tree with their unit tests. Legacy shell/workspace/editor code is
also retained, but the app does not construct it or render a scene. Former
CAD UI workflows are unavailable until a redesigned presentation is wired
in deliberately. Truffle menu work is independent and is not integrated.

`truffle-studio --smoke` checks a clear-only presentation with no draw
batches, exact opaque native background pixels at several window sizes,
idle/minimize/restore/resize behavior, inert former UI input and native close
routing. Host/font tests and independent CAD/kernel unit tests remain useful.

## Documentation

- `PROJECT_CONTEXT.md`: purpose and boundaries
- `AGENTS.md`: contribution and delivery rules
- `docs/handoff.md`: current state and next steps

## License

Apache License 2.0. See `LICENSE`.

Studio links OpenCASCADE dynamically, under the LGPL-2.1 with the
OpenCASCADE exception (see `external/occt/LICENSE_LGPL_21.txt` and
`external/occt/OCCT_LGPL_EXCEPTION.txt`).
