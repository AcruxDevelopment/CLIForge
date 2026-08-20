@echo off
REM Run an executable module by its CMake target name (as registered in
REM that module's own CMakeLists.txt), resolved via the manifest that
REM _write_module_manifest() generates at configure time -- so this script
REM never needs updating when modules are added, removed, or renamed.
REM
REM Usage (from the project root):
REM   scripts\run.bat <target-name> [-- program-args...]
REM
REM Examples:
REM   scripts\run.bat hello_exe
REM   scripts\run.bat farewell_exe -- Ale
setlocal enabledelayedexpansion

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"

if "%~1"=="" goto usage

set "TARGET_NAME=%~1"
shift

REM Collect any remaining args to pass through to the program (skipping a
REM leading "--" separator if present).
set "PASSTHROUGH="
set "skip_next_dashes=1"
:collect
if "%~1"=="" goto collected
if "!skip_next_dashes!"=="1" if "%~1"=="--" (
    set "skip_next_dashes=0"
    shift
    goto collect
)
set "skip_next_dashes=0"
set "PASSTHROUGH=!PASSTHROUGH! "%~1""
shift
goto collect
:collected

set "MANIFEST="
for %%F in ("%BUILD_DIR%\module_manifest-*.txt") do (
    if not defined MANIFEST set "MANIFEST=%%F"
)
if not defined MANIFEST (
    echo error: no build manifest found in "%BUILD_DIR%". Run scripts\build.bat first. 1>&2
    exit /b 1
)

set "BIN_PATH="
for /f "usebackq tokens=1,* delims==" %%K in ("%MANIFEST%") do (
    if "%%K"=="%TARGET_NAME%" set "BIN_PATH=%%L"
)

if not defined BIN_PATH (
    echo error: no executable target named "%TARGET_NAME%". 1>&2
    echo Available targets: 1>&2
    for /f "usebackq tokens=1 delims==" %%T in ("%MANIFEST%") do echo   %%T 1>&2
    exit /b 1
)

if not exist "%BIN_PATH%" (
    echo error: "%BIN_PATH%" was not found. Rebuild with scripts\build.bat. 1>&2
    exit /b 1
)

"%BIN_PATH%" !PASSTHROUGH!
exit /b %ERRORLEVEL%

:usage
echo Usage: %~nx0 ^<cmake-target-name^> [-- program-args...] 1>&2
echo. 1>&2
echo Available executable targets: 1>&2
set "found=0"
for %%F in ("%BUILD_DIR%\module_manifest-*.txt") do (
    set "found=1"
    for /f "usebackq tokens=1 delims==" %%T in ("%%F") do echo   %%T 1>&2
)
if "!found!"=="0" echo   (none found -- did you run scripts\build.bat?) 1>&2
exit /b 1
