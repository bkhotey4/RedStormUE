@echo off
chcp 65001 >nul
set UE=C:\Program Files\Epic Games\UE_5.4
rem 專案路徑含中文（使用者名稱）會讓 MSVC 失敗，所以先對應到純英文磁碟機 R:
subst R: /D >nul 2>nul
subst R: "%~dp0.."
if not exist R:\RedStormUE\RedStorm.uproject (
  echo 無法建立 R: 磁碟機對應
  pause
  exit /b 1
)
if not exist R:\RedStormUE\Temp mkdir R:\RedStormUE\Temp
set VSLANG=1033
set TMP=R:\RedStormUE\Temp
set TEMP=R:\RedStormUE\Temp
echo 正在編譯 紅色風暴 Unreal 版（第一次約需 5~15 分鐘）...
call "%UE%\Engine\Build\BatchFiles\Build.bat" RedStormEditor Win64 Development -Project="R:\RedStormUE\RedStorm.uproject" -WaitMutex -NoHotReload > "R:\RedStormUE\build_log.txt" 2>&1
if errorlevel 1 (
  echo 編譯失敗，錯誤記錄在 build_log.txt（請告訴 Claude 幫你修正）
) else (
  echo 編譯成功！雙擊「開啟_RedStorm_編輯器.bat」或「直接遊玩_RedStorm.bat」
)
echo BUILD_FINISHED>> "R:\RedStormUE\build_log.txt"
pause
