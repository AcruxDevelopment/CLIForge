@echo off
REM Install this project's own build artifacts to
REM out\install\<platform>-<arch>-<compiler>\ (INSTALL_OUTPUT_DIR in
REM Configuration.cmake; see CMakeLists.txt's install() DESTINATION paths).
REM
REM Always passes --component to `cmake --install`. Without it, any
REM vendored/fetched dependency's own install() rules (see
REM cmake\Dependencies.cmake) would also run, typically dumping files under
REM CMAKE_INSTALL_PREFIX -- COMPONENT is what keeps `cmake --install`
REM scoped to just this project's own targets.
REM
REM Usage (from the project root):
REM   scripts\install.bat [BUILD_TYPE]
setlocal

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"
set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"
set "COMPONENT=demo"

if not exist "%BUILD_DIR%" (
    echo error: "%BUILD_DIR%" not found. Run scripts\build.bat first.
    exit /b 1
)

echo ==^> Installing (component: %COMPONENT%)
cmake --install "%BUILD_DIR%" --config %BUILD_TYPE% --component %COMPONENT%
exit /b %ERRORLEVEL%
