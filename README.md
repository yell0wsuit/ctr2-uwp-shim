# ctr2-uwp-shim

A DLL shim that allows the Windows 8.1 Store version of **Cut the Rope 2** as a desktop game. The shim supplies the Windows Runtime features the game needs through two DLLs placed beside the game's `Game.exe`. The original executable stays unchanged.

Windows supports gameplay, saves, resizable windows, and borderless fullscreen. Wine support is **experimental** and may have bugs.

The game and Microsoft runtime files are not included in this repo.

## Set up and play

### Windows

You can use the prebuilt DLLs without compiling the shim.

1. Extract Cut the Rope 2 UWP package into a folder such as `C:\Games\CutTheRope2UWP`.

   The game is available from Microsoft Store version at <https://apps.microsoft.com/detail/9nblggh0j0dc>.

   An `.appx` / `.appxbundle` is a ZIP archive, you can rename it to `.zip` before extracting.

2. Download [`vclibs_redist_packages.zip`](https://download.microsoft.com/download/5/f/0/5f0f8404-9329-44a9-8176-ed6f7f746f25/vclibs_redist_packages.zip) and extract `Microsoft.VCLibs.120.00_12.0.21005.1_x86__8wekyb3d8bbwe.appx`. Then copy these files beside `Game.exe`: `msvcr120_app.dll`, `msvcp120_app.dll`, and `vccorlib120_app.dll`. Keep the game's `Assets` folder and other extracted files together.

3. Under **Assets** on the [latest release](https://github.com/yell0wsuit/ctr2-uwp-shim/releases/latest), download `ctr2-uwp-shim-vYEAR.MONTH.DAY-x86.zip` (the version may also have a suffix such as `-1`). Extract both `ctr2shim.dll` and `d3dcompiler_47.dll` beside `Game.exe`. The source code archives do not contain built DLLs. To build them yourself, see the [building guide](docs/building.md#windows).

4. Check for percent-encoded asset names left by raw `.appx` extraction. In `Assets\Maps`, rename `%21_MasterGameDesignDocument.xml` to `!_MasterGameDesignDocument.xml` if present. Decode any other percent-encoded asset names too; the setup script below does this automatically.

5. Double-click `Game.exe` to play. Close the game before updating the shim, then replace both shim DLLs with the ones from the same release.

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

Saves go to `SaveData` beside `Game.exe`. To change the location, edit `SaveLocation` under `[storage]` in `ctr2shim.ini` beside the game:

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

- **Setup reports a missing file:** check that the source folder contains `Game.exe`, `Assets`, and all three x86 runtime DLLs listed above.
- **The game crashes after loading:** rerun setup to ensure asset names are decoded. Or rename the game's map file from `%21` to `!` (exclamation mark) in `Assets\Maps` if you extracted the game from an `.appx`.
- **Need a log:** run `python scripts/play.py --log`. The log is written to `ctr2shim.log` beside `Game.exe`. Use `--trace` for detailed call logging or `--diagnostics` for crash details; both also enable logging.

Run `python scripts/play.py --help` for all launcher options.  
See the [building guide](docs/building.md) for compilation, tests, and code generation.

## License

The shim is licensed under the [MIT License](LICENSE).
The game and Microsoft runtime files retain their own licenses.
