#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot '../local/game'),
    [switch]$NoTrace,
    [ValidateSet('shim', 'trace')][string]$Mode = 'shim'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$game = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
foreach ($name in @('Game.exe', 'ctr2shim.dll', 'd3dcompiler_47.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $name) -PathType Leaf)) { throw "Missing $name; run setup.ps1 first." }
}
$previousMode = [Environment]::GetEnvironmentVariable('CTR2SHIM_MODE', 'Process')
$previousTrace = [Environment]::GetEnvironmentVariable('CTR2SHIM_TRACE', 'Process')
try {
    $env:CTR2SHIM_MODE = $Mode
    $env:CTR2SHIM_TRACE = '1'
    if ($NoTrace) { $env:CTR2SHIM_TRACE = '0' }
    $process = Start-Process -FilePath (Join-Path $game 'Game.exe') -WorkingDirectory $game -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Game.exe exited with code $($process.ExitCode). See $game\ctr2shim.log" }
} finally {
    [Environment]::SetEnvironmentVariable('CTR2SHIM_MODE', $previousMode, 'Process')
    [Environment]::SetEnvironmentVariable('CTR2SHIM_TRACE', $previousTrace, 'Process')
}
