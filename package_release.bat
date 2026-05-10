@echo off
rem ============================================================
rem  Build + package a redistributable zip for end users.
rem  Output: dist\QuickJumpFolders-<version>.zip
rem ============================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

set VERSION=v1.0
if not "%~1"=="" set VERSION=%~1

echo ============================================================
echo  Packaging QuickJumpFolders %VERSION%
echo ============================================================
echo.

rem Step 1: build
echo [1/3] Building...
call build.bat
if errorlevel 1 (
    echo Build failed. Aborting.
    exit /b 1
)

if not exist build\watcher_tray.exe (
    echo [ERROR] build\watcher_tray.exe not found after build.
    pause
    exit /b 1
)

rem Step 2: stage files into dist\QuickJumpFolders-<version>\
echo.
echo [2/3] Staging files...

set STAGE=dist\QuickJumpFolders-%VERSION%
if exist "%STAGE%" rmdir /S /Q "%STAGE%"
mkdir "%STAGE%"

copy /Y build\watcher_tray.exe "%STAGE%\"  >nul
copy /Y config\watch_dirs.txt  "%STAGE%\"  >nul
copy /Y run.bat                "%STAGE%\"  >nul
copy /Y dist\USAGE.txt         "%STAGE%\"  >nul
if exist LICENSE copy /Y LICENSE "%STAGE%\LICENSE.txt" >nul

rem run.bat in the package needs to find watcher_tray.exe in the same folder,
rem not in a "build\" subfolder. Patch it in-place.
powershell -NoProfile -Command ^
  "(Get-Content '%STAGE%\run.bat' -Raw -Encoding UTF8) -replace 'build\\watcher_tray.exe', 'watcher_tray.exe' | Set-Content '%STAGE%\run.bat' -Encoding UTF8" 2>nul

rem Step 3: zip it
echo.
echo [3/3] Creating zip...

set ZIPFILE=dist\QuickJumpFolders-%VERSION%.zip
if exist "%ZIPFILE%" del /Q "%ZIPFILE%"

powershell -NoProfile -Command ^
  "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIPFILE%' -Force"

if not exist "%ZIPFILE%" (
    echo [ERROR] Failed to create zip.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  [OK] Package created:
for %%f in ("%ZIPFILE%") do echo    %%~ff   (%%~zf bytes)
echo.
echo  Upload this zip to GitHub Releases.
echo ============================================================
pause
