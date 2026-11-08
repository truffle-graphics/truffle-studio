param([switch]$Check)
$ErrorActionPreference = 'Stop'
try {
    function Require-Tool([string]$Command, [string]$Package) {
        if (Get-Command $Command -ErrorAction SilentlyContinue) { return }
        if ($Check) { throw "Missing $Command. Run .\setup.ps1." }
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw "Install $Package manually, then rerun setup." }
        if ((Read-Host "Install $Package with winget? [y/N]") -notin @('y', 'Y')) { throw 'Installation declined.' }
        & winget install --exact --id $Package
        if ($LASTEXITCODE -ne 0) { throw "Installation of $Package failed." }
        $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
        if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) { throw "Restart PowerShell after installing $Package." }
    }
    . (Join-Path $PSScriptRoot 'cmake\WindowsToolchain.ps1')
    Initialize-StudioWindowsToolchain
    Require-Tool git Git.Git
    Require-Tool cmake Kitware.CMake
    Require-Tool ninja Ninja-build.Ninja
    function Prepare-Submodule([string]$Path) {
        $entry = & git ls-files --stage -- $Path
        if ($LASTEXITCODE -ne 0 -or $entry -notmatch '^160000 ([0-9a-f]+) ') { throw "No pinned submodule at $Path." }
        $expected = $Matches[1]
        $actual = ''
        if (Test-Path "$Path\.git") {
            $actual = & git -C $Path rev-parse HEAD
            if ($LASTEXITCODE -ne 0) { throw "Cannot read $Path HEAD." }
        }
        if ($actual -eq $expected) { return }
        if ($Check) { throw "Missing or out-of-date submodule: $Path." }
        if ($actual) {
            $dirty = & git -C $Path status --porcelain
            if ($LASTEXITCODE -ne 0 -or $dirty) { throw "Refusing to move dirty submodule $Path." }
        }
        & git submodule update --init -- $Path
        if ($LASTEXITCODE -ne 0) { throw "Submodule preparation failed: $Path." }
    }
    Push-Location $PSScriptRoot
    try {
        & cmake --list-presets
        if ($LASTEXITCODE -ne 0) { throw 'CMake 3.23 or newer is required.' }
        foreach ($path in @('external\truffle', 'external\SDL', 'external\occt')) { Prepare-Submodule $path }
        Push-Location external\truffle
        try {
            foreach ($path in @('vendor\freetype', 'vendor\harfbuzz', 'vendor\sheenbidi', 'vendor\utf8proc', 'vendor\vulkan-headers', 'vendor\volk', 'vendor\glslang')) {
                Prepare-Submodule $path
            }
        } finally { Pop-Location }
    } finally { Pop-Location }
    Write-Output 'Setup ready. Run .\build.ps1 debug -WarningsAsErrors on --parallel 4.'
} catch { Write-Error $_; exit 1 }
