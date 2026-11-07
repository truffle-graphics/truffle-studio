[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Position = 0)]
    [string]$Preset = 'debug',
    [ValidateSet('on', 'off')]
    [string]$WarningsAsErrors,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$BuildArguments
)
$ErrorActionPreference = 'Stop'
try {
    if ($Preset -in @('--help', '-h') -or
        $BuildArguments -contains '--help' -or $BuildArguments -contains '-h') {
        Write-Output 'Usage: .\build.ps1 [debug|release|relwithdebinfo] [-WarningsAsErrors on|off] [CMake build arguments]'
        exit 0
    }
    . (Join-Path $PSScriptRoot 'cmake\WindowsToolchain.ps1')
    Initialize-StudioWindowsToolchain
    foreach ($tool in @('cmake', 'ninja')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. Run .\setup.ps1." }
    }
    Push-Location $PSScriptRoot
    try {
        $arguments = @('--preset', $Preset)
        if ($WarningsAsErrors) { $arguments += "-DTFLST_WARNINGS_AS_ERRORS=$($WarningsAsErrors.ToUpperInvariant())" }
        & cmake @arguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build --preset $Preset @BuildArguments
        exit $LASTEXITCODE
    } finally { Pop-Location }
} catch { Write-Error $_; exit 1 }
