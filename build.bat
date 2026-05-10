@echo off
rem ============================================================
rem  QuickJumpFolders one-click build script
rem
rem  Looks for a C++ compiler in this order:
rem    1. cl.exe already in PATH (current shell has VS env)
rem    2. Visual Studio 2017/2019/2022 (Community/Pro/Enterprise)
rem    3. Build Tools for Visual Studio 2017/2019/2022
rem
rem  Usage: double-click build.bat
rem ============================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ============================================================
echo  QuickJumpFolders build
echo ============================================================
echo.

rem ---- Stage 1: cl.exe already on PATH? ----
where cl.exe >nul 2>nul
if %errorlevel%==0 (
    echo [1/4] cl.exe already on PATH, building directly
    goto :build
)

rem ---- Stage 2: use vswhere to find VS or Build Tools ----
echo [1/4] Searching for Visual Studio / Build Tools...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VSPATH="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "VSPATH=%%i"
    )
)

if defined VSPATH (
    echo       Found: !VSPATH!
    set "VCVARS=!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
    if exist "!VCVARS!" (
        echo [2/4] Initializing VS build environment...
        call "!VCVARS!" >nul
        if !errorlevel! neq 0 (
            echo [ERROR] vcvars64.bat failed
            goto :no_compiler
        )
        goto :verify
    ) else (
        echo [WARN] vcvars64.bat not found: !VCVARS!
    )
)

rem ---- Stage 3: fallback - scan known install paths ----
echo [INFO] vswhere did not find anything, scanning known paths...

for %%E in (Community Professional Enterprise BuildTools) do (
    for %%V in (2022 2019 2017) do (
        set "TRY=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!TRY!" (
            echo       Found: !TRY!
            call "!TRY!" >nul
            if !errorlevel!==0 goto :verify
        )
        set "TRY=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!TRY!" (
            echo       Found: !TRY!
            call "!TRY!" >nul
            if !errorlevel!==0 goto :verify
        )
    )
)

goto :no_compiler

:verify
echo [3/4] Verifying cl.exe...
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] cl.exe still missing after vcvars64
    goto :no_compiler
)

:build
echo [4/4] Compiling
echo.

if not exist build mkdir build

set COMMON_FLAGS=/std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /nologo /W3 /MT

echo --- Building watcher_tray.exe ---
cl %COMMON_FLAGS% watcher\watcher_tray.cpp ^
   /Fo:build\ /Fe:build\watcher_tray.exe ^
   /link User32.lib Shell32.lib Comctl32.lib Gdi32.lib Ole32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
if errorlevel 1 goto :fail

del /Q build\*.obj 2>nul

if not exist build\watch_dirs.txt (
    if exist config\watch_dirs.txt copy /Y config\watch_dirs.txt build\ >nul
)

echo.
echo ============================================================
echo  [OK] Build succeeded. Artifacts:
dir /b build\*.exe
echo.
echo  Next step: double-click run.bat
echo ============================================================
pause
exit /b 0

:no_compiler
echo.
echo ============================================================
echo  [ERROR] No C++ compiler found
echo ============================================================
echo.
echo No MSVC compiler was detected on this machine.
echo.
echo Note: VSCode (Visual Studio Code, the blue editor) is NOT the
echo       same thing as Visual Studio (the purple IDE). VSCode does
echo       not ship with a C++ compiler.
echo.
echo Please install one of the following:
echo.
echo   Option A (recommended, ~3GB): Visual Studio Community 2022
echo     Download: https://visualstudio.microsoft.com/downloads/
echo     Pick "Community 2022" (free), and during install check:
echo       [x] Desktop development with C++
echo.
echo   Option B (lightweight, ~1.5GB, CLI only): Build Tools 2022
echo     Download: https://visualstudio.microsoft.com/downloads/
echo     Scroll to "Tools for Visual Studio 2022", pick
echo     "Build Tools for Visual Studio 2022", and check:
echo       [x] Desktop development with C++
echo.
echo After installing, double-click build.bat again.
echo ============================================================
pause
exit /b 1

:fail
echo.
echo [ERROR] Build failed. Please copy the error messages above.
echo.
echo Common cause:
echo   * watcher_tray.exe is still running -- close it (right-click tray
echo     icon -> Exit) or kill it via Task Manager, then rebuild.
echo.
pause
exit /b 1
