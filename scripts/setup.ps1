#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build/windows-x86')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-FullFileSystemPath([string]$Path) {
    if ($PWD.Provider.Name -ne 'FileSystem') { throw 'Run setup from a filesystem directory.' }
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}
function Assert-NoReparseAncestors([string]$Path) {
    $current = $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed in staging paths: $current"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current.TrimEnd('\', '/'))
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Assert-NoReparseTree([string]$Path) {
    # Inspect each directory before descending; do not traverse junctions or symlinks.
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($Path)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed in source or destination: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}
function Assert-X86PE([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing runtime file: $Path" }
    Assert-NoReparseAncestors $Path
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file: $Path" }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x014C) {
            throw "Expected an x86 (I386) PE file: $Path"
        }
    } finally { $reader.Dispose() }
}
function Copy-AtomicDll([string]$Source, [string]$Target) {
    $temporary = "$Target.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::Copy($Source, $temporary, $false)
        if ([IO.File]::Exists($Target)) {
            # [NullString]: PowerShell turns $null into "" for string arguments.
            [IO.File]::Replace($temporary, $Target, [NullString]::Value)
        } else {
            [IO.File]::Move($temporary, $Target)
        }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

$source = Get-FullFileSystemPath $SourceDirectory
$destinationPath = Get-FullFileSystemPath $Destination
$build = Get-FullFileSystemPath $BuildDirectory
foreach ($path in @($source, $destinationPath, $build)) { Assert-NoReparseAncestors $path }
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Missing game directory: $source" }
$sourcePrefix = $source.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$destinationPrefix = $destinationPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($sourcePrefix.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationPrefix.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source and destination must not overlap; use a separate staging directory.'
}
Assert-NoReparseTree $source
if (Test-Path -LiteralPath $destinationPath) {
    if (-not (Test-Path -LiteralPath $destinationPath -PathType Container)) { throw 'Destination must be a directory.' }
    Assert-NoReparseTree $destinationPath
}
foreach ($name in @('Game.exe', 'msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll')) {
    Assert-X86PE (Join-Path $source $name)
}
foreach ($name in @('ctr2shim.dll', 'd3dcompiler_47.dll')) { Assert-X86PE (Join-Path $build $name) }
if (-not (Test-Path -LiteralPath (Join-Path $source 'Assets') -PathType Container)) { throw 'Source game Assets directory is missing.' }
$originalHash = (Get-FileHash -LiteralPath (Join-Path $source 'Game.exe') -Algorithm SHA256).Hash
[IO.Directory]::CreateDirectory($destinationPath) | Out-Null
# Runtime whitelist: do not copy decompiles, certificates, package manifests, or keys.
foreach ($name in @('Game.exe', 'msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll',
    'App.xbf', 'MainPage.xbf', 'MovieMgrPage.xbf', 'VideoPlayerPage.xbf', 'resources.pri', 'ZF.winmd', 'VungleSDK.winmd')) {
    $path = Join-Path $source $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { Copy-Item -LiteralPath $path -Destination (Join-Path $destinationPath $name) -Force }
}
$assetsDestination = Join-Path $destinationPath 'Assets'
[IO.Directory]::CreateDirectory($assetsDestination) | Out-Null
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $source 'Assets') -Force) {
    Copy-Item -LiteralPath $item.FullName -Destination $assetsDestination -Recurse -Force
}
# A raw .appx extraction keeps OPC percent-encoded names (%21_MasterGameDesignDocument.xml).
# The installer decodes them, and the game opens the decoded names; missing design
# XML leaves the game without its candy definition and crashes it after loading.
$encoded = Get-ChildItem -LiteralPath $assetsDestination -Recurse -Force | Where-Object { $_.Name -match '%[0-9A-Fa-f]{2}' }
foreach ($item in $encoded | Sort-Object { $_.FullName.Length } -Descending) {
    $decoded = [Uri]::UnescapeDataString($item.Name)
    if ($decoded.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw "Cannot decode asset name: $($item.FullName)" }
    Move-Item -LiteralPath $item.FullName -Destination (Join-Path ([IO.Path]::GetDirectoryName($item.FullName)) $decoded) -Force
}
# Settings template, copied once so later edits survive re-staging.
$settings = Join-Path $destinationPath 'ctr2shim.ini'
if (-not (Test-Path -LiteralPath $settings)) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ctr2shim.ini') -Destination $settings }
foreach ($name in @('ctr2shim.dll', 'd3dcompiler_47.dll')) {
    Copy-AtomicDll (Join-Path $build $name) (Join-Path $destinationPath $name)
}
$stagedHash = (Get-FileHash -LiteralPath (Join-Path $destinationPath 'Game.exe') -Algorithm SHA256).Hash
$sourceHash = (Get-FileHash -LiteralPath (Join-Path $source 'Game.exe') -Algorithm SHA256).Hash
if ($originalHash -ne $stagedHash -or $originalHash -ne $sourceHash) { throw 'Game.exe SHA-256 verification failed.' }
Write-Output "Prepared $destinationPath"
Write-Output "Game.exe SHA-256: $stagedHash (source and staged copy match)"
