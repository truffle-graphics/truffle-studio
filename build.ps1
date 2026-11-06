param(
    [string]$Preset = 'debug',
    [ValidateSet('on', 'off')]
    [string]$WarningsAsErrors,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BuildArguments
)
Write-Error 'Truffle Studio currently supports macOS on Apple Silicon only. Use ./build.sh there; Windows support is not implemented.'
exit 1
