@echo off
subst R: /D >nul 2>nul
subst R: "%~dp0.."
set UE=C:\Program Files\Epic Games\UE_5.4
if not exist "R:\RedStormUE\Content\RedStorm\Terrain\M_RSWater.uasset" (
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "R:\RedStormUE\RedStorm.uproject" -run=pythonscript -script="R:\RedStormUE\Tools\make_terrain_material.py" -unattended -nop4 -nosplash -NoSound > "R:\RedStormUE\Saved\terrain_material_cmd.txt" 2>&1
)
start "" "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe" "R:\RedStormUE\RedStorm.uproject" -game -windowed -ResX=1600 -ResY=900 -log
