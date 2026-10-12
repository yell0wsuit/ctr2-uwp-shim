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
