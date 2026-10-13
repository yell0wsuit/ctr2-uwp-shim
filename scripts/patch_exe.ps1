#requires -Version 3.0
# Creates Game.desktop.exe; the original executable is never written to.
[CmdletBinding()]
param([string]$Exe = '.\Game.exe', [switch]$IncludeRuntime)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Convert-DesktopImage([byte[]]$Original, [bool]$IsDll) {
    if ($Original.Length -lt 64 -or $Original[0] -ne 0x4D -or $Original[1] -ne 0x5A) {
        throw 'Expected a DOS/PE image.'
    }
    $pe = [long][BitConverter]::ToUInt32($Original, 0x3C)
    if ($pe -lt 64 -or $pe + 24 -gt $Original.Length) { throw 'Invalid or truncated PE header.' }
    if ([BitConverter]::ToUInt32($Original, [int]$pe) -ne 0x4550) { throw 'Invalid PE signature.' }
    $size = [BitConverter]::ToUInt16($Original, [int]($pe + 20))
    $characteristics = [BitConverter]::ToUInt16($Original, [int]($pe + 22))
    $optional = $pe + 24
    if ($size -lt 96 -or $optional + $size -gt $Original.Length) { throw 'Invalid or truncated PE optional header.' }
    if ([BitConverter]::ToUInt16($Original, [int]($pe + 4)) -ne 0x14C -or
        [BitConverter]::ToUInt16($Original, [int]$optional) -ne 0x10B -or
        ($characteristics -band 2) -eq 0 -or
        (($characteristics -band 0x2000) -ne 0) -ne $IsDll) {
        throw 'Expected an x86 PE32 image of the appropriate EXE/DLL type.'
    }
    $patched = [byte[]]$Original.Clone()
    $patched[[int]($optional + 71)] = $patched[[int]($optional + 71)] -band 0xEF
    return ,$patched
}

try {
    $source = (Resolve-Path -LiteralPath $Exe).ProviderPath
    if ($IncludeRuntime) {
        $folder = [IO.Path]::GetDirectoryName($source)
        $destination = Join-Path $folder 'desktop'
        if (Test-Path -LiteralPath $destination) { throw "$destination already exists; move it before retrying." }
        $runtimeNames = @('msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll')
        $images = @{}
        $originals = @{}
        $originals['Game.desktop.exe'] = [IO.File]::ReadAllBytes($source)
        $images['Game.desktop.exe'] = Convert-DesktopImage $originals['Game.desktop.exe'] $false
        foreach ($name in $runtimeNames) {
            $originals[$name] = [IO.File]::ReadAllBytes((Join-Path $folder $name))
            $images[$name] = Convert-DesktopImage $originals[$name] $true
        }
        foreach ($name in @('ctr2shim.dll', 'd3dcompiler_47.dll', 'Assets')) {
            if (-not (Test-Path -LiteralPath (Join-Path $folder $name))) { throw "Missing $name beside the source executable." }
        }
        # Copy only runtime content, not package metadata or decompiled sources.
        # Reject reparse points before recursively copying assets or saves.
        $copyNames = @('ctr2shim.dll', 'd3dcompiler_47.dll', 'Assets', 'SaveData', 'ctr2shim.ini',
            'App.xbf', 'MainPage.xbf', 'MovieMgrPage.xbf', 'VideoPlayerPage.xbf',
            'resources.pri', 'ZF.winmd', 'VungleSDK.winmd')
        foreach ($name in $copyNames) {
            $path = Join-Path $folder $name
            if (Test-Path -LiteralPath $path) {
                $pending = New-Object 'System.Collections.Generic.Stack[string]'
                $pending.Push($path)
                while ($pending.Count) {
                    $item = Get-Item -LiteralPath $pending.Pop() -Force
                    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cannot copy reparse point: $($item.FullName)" }
                    if ($item.PSIsContainer) {
                        foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) { $pending.Push($child.FullName) }
                    }
                }
            }
        }
        New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
        foreach ($name in $copyNames) {
            $path = Join-Path $folder $name
            if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $destination $name) -Recurse -Force }
        }
        foreach ($name in $images.Keys) {
            $target = Join-Path $destination $name
            $stream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $stream.Write($images[$name], 0, $images[$name].Length) } finally { $stream.Dispose() }
            if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($target)) -cne [Convert]::ToBase64String($images[$name])) {
                throw "Verification failed for $target; do not launch the copy."
            }
            $originalPath = if ($name -eq 'Game.desktop.exe') { $source } else { Join-Path $folder $name }
            if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($originalPath)) -cne [Convert]::ToBase64String($originals[$name])) {
                throw "Source changed during preparation: $originalPath"
            }
        }
        Write-Output "Created $destination; original executable and runtime DLLs unchanged."
        Write-Output "Launch $destination\Game.desktop.exe --log"
        exit 0
    }
    $output = Join-Path ([IO.Path]::GetDirectoryName($source)) (
        [IO.Path]::GetFileNameWithoutExtension($source) + '.desktop' + [IO.Path]::GetExtension($source))
    $original = [IO.File]::ReadAllBytes($source)
    if ($original.Length -lt 64 -or $original[0] -ne 0x4D -or $original[1] -ne 0x5A) {
        throw 'Expected a DOS/PE executable.'
    }
    # Use Int64 arithmetic so malformed offsets cannot overflow the bounds checks.
    $pe = [long][BitConverter]::ToUInt32($original, 0x3C)
    if ($pe -lt 64 -or $pe + 24 -gt $original.Length) { throw 'Invalid or truncated PE header.' }
    if ([BitConverter]::ToUInt32($original, [int]$pe) -ne 0x4550) { throw 'Invalid PE signature.' }
    $machine = [BitConverter]::ToUInt16($original, [int]($pe + 4))
    $size = [BitConverter]::ToUInt16($original, [int]($pe + 20))
    $characteristics = [BitConverter]::ToUInt16($original, [int]($pe + 22))
    $optional = $pe + 24
    if ($size -lt 96 -or $optional + $size -gt $original.Length) {
        throw 'Invalid or truncated PE optional header.'
    }
    $magic = [BitConverter]::ToUInt16($original, [int]$optional)
    if ($machine -ne 0x14C -or $magic -ne 0x10B -or
        ($characteristics -band 2) -eq 0 -or ($characteristics -band 0x2000) -ne 0) {
        throw 'Expected an x86 PE32 executable, not a DLL.'
    }
    $offset = [int]($optional + 70)
    $flags = [BitConverter]::ToUInt16($original, $offset)
    if (($flags -band 0x1000) -eq 0) {
        Write-Output 'AppContainer flag is already clear; no copy created.'
        exit 0
    }
    if (Test-Path -LiteralPath $output) { throw "$output already exists; move or delete it before retrying." }
    $patched = [byte[]]$original.Clone()
    # Only clear bit 4 in the high byte of DllCharacteristics (0x1000).
    $patched[$offset + 1] = $patched[$offset + 1] -band 0xEF
    $stream = [IO.File]::Open($output, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $stream.Write($patched, 0, $patched.Length)
    } catch {
        $stream.Dispose()
        [IO.File]::Delete($output)
        throw
    } finally {
        $stream.Dispose()
    }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($output)) -cne [Convert]::ToBase64String($patched) -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($source)) -cne [Convert]::ToBase64String($original)) {
        throw 'File verification failed; do not launch the desktop copy.'
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash($original)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    Write-Output "Created $output"
    Write-Output ('DllCharacteristics: 0x{0:X4} -> 0x{1:X4}' -f $flags, ($flags -band 0xEFFF))
    Write-Output "Original unchanged. SHA-256: $hash"
    Write-Output 'Launch the desktop copy beside the shim DLLs and game assets.'
    Write-Output 'This clears the launch requirement; Windows 8/8.1 gameplay still needs testing.'
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
