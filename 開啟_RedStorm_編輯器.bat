@echo off
subst R: /D >nul 2>nul
subst R: "%~dp0.."
start "" "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe" "R:\RedStormUE\RedStorm.uproject"
