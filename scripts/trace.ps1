#requires -Version 5.1
[CmdletBinding()]
param([string]$GameDirectory = (Join-Path $PSScriptRoot '../local/game'))
$ErrorActionPreference = 'Stop'
Write-Warning 'Activation-only passthrough trace: typed method calls and returned object interfaces are not traced. Native Windows reference execution is unverified; unpackaged XAML activation may fail.'
& (Join-Path $PSScriptRoot 'run.ps1') -GameDirectory $GameDirectory -Passthrough
