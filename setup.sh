#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
check=false
case "${1:-}" in
    --check) check=true ;;
    --help|-h)
        echo "Usage: ./setup.sh [--check]"
        echo "Prepare Studio; --check verifies without installing or fetching."
        exit 0 ;;
    "") ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
(( $# <= 1 )) || { echo "Too many arguments." >&2; exit 2; }
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    echo "Truffle Studio currently supports macOS on Apple Silicon only." >&2
    exit 1
fi
confirm() {
    local answer
    if ! read -r -p "$1 [y/N] " answer; then
        echo "Setup needs confirmation. Rerun in a terminal or install the tools manually." >&2
        return 1
    fi
    if [[ "$answer" != y && "$answer" != Y ]]; then
        echo "Installation declined; no dependencies were installed." >&2
        return 1
    fi
}
if ! xcrun --find clang++ >/dev/null 2>&1; then
    if $check; then echo "Missing Xcode command-line tools." >&2; exit 1; fi
    confirm "Open Apple's installer for Xcode command-line tools?" || exit 1
    xcode-select --install
    echo "Finish Apple's installation, then rerun ./setup.sh."
    exit 1
fi
packages=()
for tool in git cmake ninja; do
    command -v "$tool" >/dev/null || packages+=("$tool")
done
if (( ${#packages[@]} )); then
    if $check; then echo "Missing tools: ${packages[*]}" >&2; exit 1; fi
    command -v brew >/dev/null || {
        echo "Install Homebrew from https://brew.sh, or install ${packages[*]} manually, then rerun." >&2
        exit 1
    }
    confirm "Install missing tools with: brew install ${packages[*]}?" || exit 1
    brew install "${packages[@]}"
fi
cmake --list-presets >/dev/null
prepare_submodule() {
    local path="$1" expected actual
    expected="$(git ls-files --stage -- "$path" | awk '$1 == "160000" {print $2}')"
    [[ -n "$expected" ]] || { echo "No pinned submodule at $path." >&2; exit 1; }
    actual=""
    if [[ -e "$path/.git" ]]; then actual="$(git -C "$path" rev-parse HEAD)"; fi
    if [[ "$actual" == "$expected" ]]; then return; fi
    if $check; then echo "Missing or out-of-date submodule: $path. Run ./setup.sh." >&2; exit 1; fi
    if [[ -n "$actual" ]]; then
        local dirty
        dirty="$(git -C "$path" status --porcelain)"
        if [[ -n "$dirty" ]]; then
            echo "Refusing to move dirty submodule $path. Preserve its changes before rerunning setup." >&2
            exit 1
        fi
    fi
    git submodule update --init -- "$path"
}
for path in external/truffle external/SDL external/occt; do prepare_submodule "$path"; done
(
    cd external/truffle
    for path in vendor/freetype vendor/harfbuzz vendor/sheenbidi vendor/utf8proc; do
        prepare_submodule "$path"
    done
)
echo "Setup ready. Run ./build.sh (debug), ./build.sh release, or ./build.sh relwithdebinfo."
