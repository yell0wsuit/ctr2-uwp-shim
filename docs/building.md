# Building the shim manually

The shim targets **32-bit Windows (x86)** with the MSVC ABI. Build on Windows with MSVC or cross-compile on macOS with `clang-cl`. Game files are not needed to build the shim or run its automated tests.

## Windows

Install Visual Studio's **Desktop development with C++** workload, including the MSVC x86 tools and a Windows SDK. You also need **CMake 3.25 or newer**, **Ninja**, and **Python 3** for script tests and code generation.

Open an **x86 Native Tools Command Prompt** for Visual Studio, change to the repository root, and run:

```bat
cmake --preset windows-x86
cmake --build --preset windows-x86
```

This creates `ctr2shim.dll` and `d3dcompiler_47.dll` in `build\windows-x86`. Follow the [setup instructions](../README.md#set-up-and-play) to copy them into a playable game folder.

If CMake cannot find `cl`, use an x86 Visual Studio developer shell. If MSYS2's CMake appears first on `PATH`, put the native Windows CMake ahead of it (typically `C:\Program Files\CMake\bin`).

### Tests

```bat
ctest --test-dir build/windows-x86 --output-on-failure
python -m unittest discover -s tests/python
```

Native tests keep saves in the build folder. PowerShell setup tests require PowerShell 7 (`pwsh`); shell setup tests are skipped on Windows.

## macOS cross-compilation

Install CMake, Ninja, LLVM, LLD, and xwin. With Homebrew:

```sh
brew install cmake ninja llvm lld xwin
```

From the repository root, download the Windows SDK and CRT, then build:

```sh
xwin --accept-license --arch x86 splat --output .sdk
cmake --preset macos-x86
cmake --build --preset macos-x86
python3 -m unittest discover -s tests/python
```

The xwin command accepts Microsoft's SDK license. Outputs are in `build/macos-x86`; these are Windows binaries and require Wine to run. Gameplay under Wine remains experimental.

The toolchain defaults to LLVM in `/opt/homebrew/opt/llvm` and SDK files in `.sdk`. Set `LLVM_ROOT` and `XWIN_ROOT` if yours are elsewhere. See the [Wine setup instructions](../README.md#macos--wine-experimental) to launch.

### Wine graphics (macOS)

The game caps Direct3D at feature level 9_3. Wine's compiler emits SM4 code without the legacy `Aon9` payload, and WineD3D's legacy shader path also fails to receive the game's D3D11 constants. Merely using Microsoft's compiler restores Aon9 but still renders black. [`wine/wined3d.patch`](../wine/wined3d.patch) instead runs the SM4 code and supplies OpenGL integer-conversion fallbacks for macOS.

Build the complete matched graphics stack with:

```sh
brew install bison llvm lld
scripts/build-wined3d.sh
```

The script downloads checksum-pinned Wine 11.18 source into `local/wine-src`, applies the patch, and builds the x86 PE `d3d11.dll`, `dxgi.dll`, and `wined3d.dll`. It does not inspect or require an installed Wine version. The first build takes a few minutes and currently uses Rosetta for the x86_64 build tools.

Outputs go to `build/macos-x86`, or the folder given as the first argument, with `wine-graphics.json` recording their SHA-256 hashes. `setup.sh` stages them together. `run.sh` loads all three as native overrides and verifies their hashes before starting the game. This keeps private graphics exports inside a matched stack while using the installed Wine for Windows APIs, windowing, and OpenGL. Keep all three DLLs and their manifest together when updating; an older installation containing only `wined3d.dll` must be rebuilt and restaged.

The source pin identifies the stack being built, not the Wine version users must install. Runtime API compatibility remains a requirement, and unsupported older or future Wine builds may still fail. The same built stack passed the FL9 shader/constant-buffer regression on Wine 11.17 and 11.18. Real game captures rendered on both, including a level scene on 11.17. The known intermittent graphics startup crash remains; these checks do not establish every Wine version or full-game parity.

`shadertest.exe` draws at feature level 9_3 using a pixel shader constant buffer and checks the resulting red pixel. It catches the shader/constant problem that a clear-only GPU test cannot detect. Under Wine, run tests with `WINEDLLOVERRIDES='d3dcompiler_47=n,b;d3d11,dxgi,wined3d=n'` so they use the same stack as the game.

### Alternative renderers and macOS 28

`python3 scripts/play.py --renderer vulkan` selects stock WineD3D's Vulkan backend using a setting specific to `Game.exe`. On macOS this requires Wine's Vulkan support and [MoltenVK](https://github.com/KhronosGroup/MoltenVK). It bypasses the supplied OpenGL stack. Testing on Wine 11.18 with MoltenVK 1.4.0 reached Vulkan initialization but failed buffer creation with `VK_ERROR_FEATURE_NOT_PRESENT` and crashed; it is not a working replacement on that configuration.

Graphics translation and CPU translation are separate. Apple says [general-purpose Rosetta support ends after macOS 27](https://developer.apple.com/news/?id=w5ngl9k2), while retaining limited functionality for certain older games. Do not assume that exception covers a Wine wrapper. Building the shim as ARM64 cannot load it into the original x86 game, and WoW64 alone does not translate Intel instructions to ARM.

For this unchanged executable, a practical route to evaluate is a native ARM virtual machine running **Windows 11 on Arm**, whose own [x86 emulation](https://learn.microsoft.com/en-us/windows/arm/apps-on-arm-x86-emulation) runs 32-bit Windows applications without macOS Rosetta. Use the Windows setup and only the two shim DLLs, not the Wine graphics stack. This game and its GPU behavior have not been tested in that environment. A native ARM game port is the longer-term option; another independent x86 CPU translator would need its own integration and validation.

## Development tools

With LLVM installed, format and lint changed C++ files (replace `<files>` with their paths):

```sh
clang-format -i <files>
clang-tidy -p build/windows-x86 --extra-arg=--target=i686-pc-windows-msvc <files>
```

For a macOS build, use `-p build/macos-x86`. Point clangd's `--compile-commands-dir` at your build folder; the checked-in VS Code settings default to `build/macos-x86`.

### Regenerate SDK interfaces

Generated interfaces are checked in, so normal builds do not need this step. After editing `shim/generated/interfaces.json`, regenerate from your Windows SDK's `winrt` headers (adjust the SDK version to your installation):

```bat
python scripts\regenerate_sdk.py "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
```

On macOS:

```sh
python3 scripts/regenerate_sdk.py .sdk/sdk/include/winrt
```
