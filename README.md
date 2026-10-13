# ctr2-uwp-shim

A DLL shim that lets you run the Windows 8.1 Store version of **Cut the Rope 2** as a desktop game. The shim supplies the Windows Runtime features the game needs through two DLLs placed beside the game's `Game.exe`. The original executable stays unchanged.

Windows supports gameplay, saves, resizable windows, and borderless fullscreen. Wine support is **experimental** and may have bugs.

The game and Microsoft runtime files are not included in this repo.

## Set up and play

### Windows

You can use the prebuilt DLLs without compiling the shim.

1. Obtain the Windows 8.1 Store package for **Cut the Rope 2**. See the game's [Microsoft Store listing](https://apps.microsoft.com/detail/9nblggh0j0dc).

   Use this package bundle:

   - File: `ZeptoLabUKLimited.CuttheRope2_2016.609.1118.0_neutral_~_sq9zxnwrk84pj.appxbundle`
   - SHA-1: `28c559f34f2ed365f4b0ce3350031c6eb6056104`

   Open the `.appxbundle` as a ZIP archive and extract `Game_1.0.0.46_Win32.appx`, the **x86** game package inside it. Then open that `.appx` as a ZIP archive and extract its contents into a folder such as `C:\Games\CutTheRope2UWP`. You can rename either archive to `.zip` if your extractor requires it. The resulting game folder should contain `Game.exe` and `Assets`.

2. Download [`vclibs_redist_packages.zip`](https://download.microsoft.com/download/5/f/0/5f0f8404-9329-44a9-8176-ed6f7f746f25/vclibs_redist_packages.zip) and extract `Microsoft.VCLibs.120.00_12.0.21005.1_x86__8wekyb3d8bbwe.appx`. Then copy these files beside `Game.exe`: `msvcr120_app.dll`, `msvcp120_app.dll`, and `vccorlib120_app.dll`. Keep the game's `Assets` folder and other extracted files together.

3. Under **Assets** on the [latest release](https://github.com/yell0wsuit/ctr2-uwp-shim/releases/latest), download the shim ZIP, for example `ctr2-uwp-shim-v2026.10.12-x86.zip`. Extract both `ctr2shim.dll` and `d3dcompiler_47.dll` beside `Game.exe`. The source code archives do not contain built DLLs. To build them yourself, see the [building guide](docs/building.md#windows).

4. Double-click `Game.exe` to play. Close the game before updating the shim, then replace both shim DLLs with the ones from the same release.

### Optional: prepare a separate game folder

With a copy of this repository, you can use setup to copy the required game files, decode asset names, and verify that `Game.exe` is unchanged. Run from the repository root, using the extracted game folder with the three runtime DLLs from step 2. Extract both shim DLLs from the release ZIP into a folder such as `downloads`, then run:

```powershell
.\scripts\setup.ps1 -SourceDirectory 'C:\Games\CutTheRope2UWP' -Destination '.\local\game' -BuildDirectory '.\downloads'
```

If you [build the shim locally](docs/building.md#windows), omit `-BuildDirectory` to use `build\windows-x86`. Double-click `local\game\Game.exe` to play. Close the game and rerun setup after updating the DLLs; existing saves and `ctr2shim.ini` are preserved.

With Python 3 installed, you can also launch from the repository with `python scripts/play.py`. For a different game folder, use `python scripts/play.py --game "C:\Games\CutTheRope2UWP"`.

### macOS / Wine (experimental)

Use the same extracted game and x86 runtime files. From the repository root, extract both shim DLLs from the release ZIP into `downloads`, then prepare and launch the game with Python 3 and Wine installed:

```sh
bash scripts/setup.sh '/path/to/CutTheRope2UWP' local/game downloads
python3 scripts/play.py
```

Alternatively, [cross-compile the shim](docs/building.md#macos-cross-compilation) and omit the final `downloads` argument to use `build/macos-x86`.

The launcher defaults to Wine Devel in `~/Applications` and stores its Wine prefix in `local/wine-test`. Set `WINE` to your Wine executable and `WINEPREFIX` to use another prefix. Wine launches always enable logging.

## Saves and window settings

Saves go to `SaveData` beside `Game.exe`. To change the location, create or edit `ctr2shim.ini` beside `Game.exe` and set `SaveLocation` under `[storage]`:

```ini
[storage]
SaveLocation=game
```

| Value | Save folder |
| --- | --- |
| `game` (default) | `SaveData` beside `Game.exe` |
| `documents` | `Documents\CutTheRope2UWP_SaveData` |
| `appdata` | `%LOCALAPPDATA%\ctr2` |
| A folder path | Your chosen folder; relative paths start beside `Game.exe` |

If the folder is not writable, the shim tries `documents`, then `appdata`. A new save location imports existing saves from `%LOCALAPPDATA%\ctr2`.

Press **F11** or **Alt+Enter** to toggle borderless fullscreen. Window size, position, and mode are remembered. The game keeps running when it loses focus and pauses when minimized. Resizing briefly reloads the game's graphics.

Window settings are stored in `Local/window.ini` inside your save folder. Close the game before editing it; `Width` and `Height` specify the game area, and `State` accepts `windowed`, `maximized`, or `fullscreen`. Delete the file with the game closed to reset window placement.

## Troubleshooting

- **Windows 8/8.1 says the app can only run in an app container:** use the built-in PowerShell with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\patch_exe.ps1 -Exe "C:\Games\CutTheRope2UWP\Game.exe"` from the repository root. The script targets PowerShell 3.0 or later and needs no Python or Visual Studio. It creates `Game.desktop.exe` beside the original, clearing only the executable's AppContainer flag. It validates the x86 PE headers, preserves `Game.exe`, and refuses to overwrite an existing copy. Launch `Game.desktop.exe --log` from the game folder with the shim DLLs and assets beside it. This addresses the launch requirement; Windows 8/8.1 gameplay remains unverified. After replacing the original game executable, delete the old desktop copy and rerun the script. The existing launch scripts still use `Game.exe`.
- **Setup reports a missing file:** check that the source folder contains `Game.exe`, `Assets`, and all three x86 runtime DLLs listed above. The folder passed as `-BuildDirectory` must contain both extracted shim DLLs.
- **The desktop copy fails with `0xC000A200`:** the bundled Visual C++ DLLs also have the AppContainer flag. Use `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\patch_exe.ps1 -Exe "C:\Games\CutTheRope2UWP\Game.exe" -IncludeRuntime`. This creates a new `desktop` subfolder containing patched copies of the executable and the three runtime DLLs, plus the shim and game content. Launch `desktop\Game.desktop.exe --log`. Original files stay unchanged; the destination must not already exist. Existing `SaveData` is copied once and the desktop copy then uses its own saves by default; custom paths in the copied `ctr2shim.ini` still apply. This mode also needs Windows verification and does not establish gameplay compatibility.
- **The game fails to start or crashes after loading:** the shim automatically repairs the two `%21` map filenames in `Assets\assets\maps`. Make sure the game folder is writable, or use the setup script to decode the filenames in a separate game folder.
- **Check which shim is installed:** right-click `ctr2shim.dll` beside `Game.exe` and open **Properties → Details**. **Product version** shows the release, such as `v2026.10.12-4`, and the commit it was built from. Both shim DLLs should show the same version.
- **Need a log:** open PowerShell in the game folder and run `.\Game.exe --log`. The log is written to `ctr2shim.log` beside `Game.exe`; the two previous runs are kept as `ctr2shim.1.log` and `ctr2shim.2.log`. Its first line names the shim version and commit. Use `--trace` for detailed call logging or `--diagnostics` for crash details; both also enable logging. With a copy of the repository and Python 3, you can also run `python scripts/play.py --game "C:\Games\CutTheRope2UWP" --log` from the repository root.

Run `python scripts/play.py --help` for all launcher options.  
See the [building guide](docs/building.md) for compilation, tests, and code generation.

## License

The shim is licensed under the [MIT License](LICENSE).
The game and Microsoft runtime files retain their own licenses.
