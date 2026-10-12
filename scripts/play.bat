@echo off
rem Starts the staged game on Windows.
rem   play.bat [--log] [--trace] [--capture] [--diagnostics] [--passthrough]
rem The game folder is local\game; set CTR2_GAME to use another one.
setlocal
if not defined CTR2_GAME set "CTR2_GAME=%~dp0..\local\game"
if not exist "%CTR2_GAME%\Game.exe" (
    echo Game.exe not found in "%CTR2_GAME%". Stage the game with scripts\setup.ps1 first.
    exit /b 1
)
start "" /D "%CTR2_GAME%" "%CTR2_GAME%\Game.exe" %*
