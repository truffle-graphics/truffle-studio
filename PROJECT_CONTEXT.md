# Project Context

## Purpose

Truffle Studio is an application built on Truffle. It replaces throwaway
editor-style samples as the place where Truffle is exercised under real use.
Long-term direction (not committed scope): a visual graphics authoring tool
with specialized workspaces such as scenes, simplified CAD and animation.

## Boundaries

- Studio owns windowing (SDL3), the event loop, fonts, documents and all
  application state. Truffle provides graphics, rendering and GUI libraries.
- Studio uses Truffle only through public headers and CMake targets.
- When Studio needs something reusable from Truffle, file a Truffle issue and
  implement it there. Do not build Truffle-shaped workarounds inside Studio,
  and do not push application concepts into Truffle.
- Truffle is a Git submodule at `external/truffle` tracking `develop`; SDL3 is
  a submodule pinned to a release tag.

## Platform

macOS on Apple Silicon with the Metal backend. Other platforms follow when
Truffle's backends and Studio's host code are verified there.

## Planning

Work is tracked as issues in this repository and on the
[Truffle delivery project](https://github.com/orgs/truffle-graphics/projects/1).
