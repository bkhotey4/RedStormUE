@echo off
chcp 65001 >nul
rem 外觀展示：關閉執行中的遊戲 → 編譯 → 以 -RSGallery 執行（截圖存到 Saved\AutoTest）
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { $_.CommandLine -like '*RedStorm.uproject*-game*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
timeout /t 3 >nul
subst R: /D >nul 2>nul
subst R: "%~dp0.."
set UE=C:\Program Files\Epic Games\UE_5.4
if not exist R:\RedStormUE\Temp mkdir R:\RedStormUE\Temp
set TMP=R:\RedStormUE\Temp
set TEMP=R:\RedStormUE\Temp
call "%UE%\Engine\Build\BatchFiles\Build.bat" RedStormEditor Win64 Development -Project="R:\RedStormUE\RedStorm.uproject" -WaitMutex -NoHotReload > "R:\RedStormUE\build_log.txt" 2>&1
if errorlevel 1 (
  echo BUILD_FAILED>> "R:\RedStormUE\build_log.txt"
  exit /b 1
)
echo BUILD_FINISHED>> "R:\RedStormUE\build_log.txt"
if not exist "R:\RedStormUE\Content\RedStorm\Terrain\M_RSWater.uasset" (
  "%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "R:\RedStormUE\RedStorm.uproject" -run=pythonscript -script="R:\RedStormUE\Tools\make_terrain_material.py" -unattended -nop4 -nosplash -NoSound > "R:\RedStormUE\Saved\terrain_material_cmd.txt" 2>&1
)
start "" "%UE%\Engine\Binaries\Win64\UnrealEditor.exe" "R:\RedStormUE\RedStorm.uproject" -game -windowed -ResX=1600 -ResY=900 -log -RSGallery
