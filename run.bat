@echo off
rem ============================================================
rem  smart_upload launcher (Quick Access pin approach)
rem
rem  This launcher starts ONLY the watcher. The watcher itself
rem  pins the latest changed folder to "Quick access" via Shell
rem  COM, so users get one click in Word/Explorer's left sidebar.
rem
rem  No DLL injection. No hooks. No admin needed.
rem
rem  If you want to additionally try the old hook approach,
rem  use run_with_hook.bat instead.
rem ============================================================

setlocal
cd /d "%~dp0"

if not exist build\watcher_tray.exe (
    echo Not built yet. Please double-click build.bat first.
    pause
    exit /b 1
)

echo Starting file watcher (Quick Access pin mode)...
start "" "build\watcher_tray.exe"

echo.
echo Done. The watcher is now running in the tray (bottom-right).
echo Modify any file under a watched folder, and that folder will
echo be pinned to "Quick access" automatically.
timeout /t 3 /nobreak >nul
