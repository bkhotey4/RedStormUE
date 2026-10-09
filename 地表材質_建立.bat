@echo off
chcp 65001 >nul
rem 地表材質：匯入 SourceArt\Terrain 的原創貼圖，並在編輯器指令列模式建立 M_RSTerrain（只需執行一次；貼圖更新後再執行）
subst R: /D >nul 2>nul
subst R: "%~dp0.."
set UE=C:\Program Files\Epic Games\UE_5.4
if not exist R:\RedStormUE\Temp mkdir R:\RedStormUE\Temp
set TMP=R:\RedStormUE\Temp
set TEMP=R:\RedStormUE\Temp
if exist R:\RedStormUE\Saved\terrain_material_log.txt del R:\RedStormUE\Saved\terrain_material_log.txt
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "R:\RedStormUE\RedStorm.uproject" -run=pythonscript -script="R:\RedStormUE\Tools\make_terrain_material.py" -unattended -nop4 -nosplash -NoSound > "R:\RedStormUE\Saved\terrain_material_cmd.txt" 2>&1
echo DONE>> "R:\RedStormUE\Saved\terrain_material_cmd.txt"
