@echo off
REM Configure (if needed) and build the project with Ninja.
REM
REM Usage (from the project root):
REM   scripts\build.bat [BUILD_TYPE]
REM
REM   BUILD_TYPE  Debug^|Release^|RelWithDebInfo^|MinSizeRel (default: Release)
REM
REM Set BUILD_DIR to build somewhere other than out\build (see Configuration.cmake).
setlocal

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"
set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

where cmake >nul 2>nul
if errorlevel 1 (
    echo error: cmake not found on PATH.
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo error: ninja not found on PATH.
    exit /b 1
)

echo ==^> Configuring (%BUILD_TYPE%) into %BUILD_DIR%\
cmake -G Ninja -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

echo ==^> Building
cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo ==^> Done. Run a module with:  scripts\run.bat ^<target-name^>
exit /b 0
