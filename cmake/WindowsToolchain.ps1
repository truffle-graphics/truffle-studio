function Initialize-StudioWindowsToolchain {
    if ($env:OS -ne 'Windows_NT') { throw 'Use ./setup.sh and ./build.sh on macOS.' }
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK, then rerun setup.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Missing Visual Studio C++ toolchain and Windows SDK.' }
    Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Visual Studio toolchain initialization failed.' }
}
