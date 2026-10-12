#requires -Version 5.1
# Starts the staged game and waits for it to exit. Logging is off unless asked for:
#   -Log          failures and notes in ctr2shim.log beside Game.exe
#   -Trace        every traced call as well (implies -Log)
#   -Capture      back buffer to ctr2-frame.bmp on the first and every 120th frame
#   -Diagnostics  access violations with stack details (implies -Log)
#   -Passthrough  activate the operating system's classes and only log
[CmdletBinding()]
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot '../local/game'),
    [switch]$Log,
    [switch]$Trace,
    [switch]$Capture,
    [switch]$Diagnostics,
    [switch]$Passthrough
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$game = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
foreach ($name in @('Game.exe', 'ctr2shim.dll', 'd3dcompiler_47.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $name) -PathType Leaf)) { throw "Missing $name; run setup.ps1 first." }
}
$arguments = @()
if ($Log) { $arguments += '--log' }
if ($Trace) { $arguments += '--trace' }
if ($Capture) { $arguments += '--capture' }
if ($Diagnostics) { $arguments += '--diagnostics' }
if ($Passthrough) { $arguments += '--passthrough' }
$start = @{ FilePath = (Join-Path $game 'Game.exe'); WorkingDirectory = $game; PassThru = $true; Wait = $true }
if ($arguments) { $start.ArgumentList = $arguments }
$process = Start-Process @start
if ($process.ExitCode -ne 0) {
    $hint = if ($arguments) { " See $game\ctr2shim.log" } else { ' Run again with -Log for details.' }
    throw "Game.exe exited with code $($process.ExitCode).$hint"
}
