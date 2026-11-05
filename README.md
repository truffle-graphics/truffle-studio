# Truffle Studio

Truffle Studio is a multi-purpose authoring application built on
[Truffle](https://github.com/truffle-graphics/truffle). CAD, animation,
simulation and design are workspaces in one application that share one
document. The first workspace being built is sketch-based CAD. It is Truffle's
dogfooding application: features Studio needs drive Truffle work, which is
tracked as Truffle issues. Truffle stays a library; Studio is the application.

Status: early foundation. macOS (Apple Silicon) with Metal only.

## Layout

Clone Studio beside the Truffle checkout:

```text
Truffle/
├── truffle/          # Truffle development checkout
└── truffle-studio/   # this repository
    └── external/
        ├── truffle/  # Truffle submodule (tracks develop)
        └── SDL/      # SDL3 submodule (pinned release)
```

## Setup

```sh
git clone https://github.com/truffle-graphics/truffle-studio.git
cd truffle-studio
git submodule update --init external/truffle external/SDL
git -C external/truffle submodule update --init \
    vendor/freetype vendor/harfbuzz vendor/sheenbidi vendor/utf8proc
```

Only the Truffle vendor submodules needed for GUI text are required; do not
use `--recursive`, which fetches Truffle's optional shader toolchains.

## Build and run

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/src/truffle-studio
ctest --preset dev
```

Requirements: CMake 3.23+, Ninja, Xcode command-line tools.

## Updating Truffle

```sh
git -C external/truffle fetch origin develop
git -C external/truffle checkout origin/develop
git add external/truffle
```

## Documentation

- `PROJECT_CONTEXT.md`: purpose and boundaries
- `AGENTS.md`: contribution and delivery rules
- `docs/handoff.md`: current state and next steps

## License

Apache License 2.0. See `LICENSE`.
