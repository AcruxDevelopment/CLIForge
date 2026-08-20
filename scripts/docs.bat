@echo off
REM Build Doxygen documentation for every module (see cmake\Documentation.cmake)
REM and print the path to the landing page. A no-op with a clear message if
REM Doxygen isn't installed or ENABLE_DOXYGEN=OFF.
REM
REM Output goes to out\docs\ (DOCS_OUTPUT_DIR in Configuration.cmake) --
REM note this is a sibling of out\build, not nested inside it. If you've
REM overridden DOCS_OUTPUT_DIR at configure time, also set DOCS_DIR here to
REM match so the printed path is correct (docs still land in the right
REM place either way -- this only affects the message below).
REM
REM Usage (from the project root):
REM   scripts\docs.bat              REM every module
REM   scripts\docs.bat core_lib      REM just one module's docs (docs_<target>)
setlocal

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"
if "%DOCS_DIR%"=="" set "DOCS_DIR=out\docs"

if not exist "%BUILD_DIR%" (
    echo error: "%BUILD_DIR%" not found. Run scripts\build.bat first.
    exit /b 1
)

if "%~1"=="" (
    cmake --build "%BUILD_DIR%" --target docs
    if exist "%DOCS_DIR%\index.html" (
        echo ==^> Open: %DOCS_DIR%\index.html
    )
) else (
    cmake --build "%BUILD_DIR%" --target docs_%1
    echo ==^> Open: %DOCS_DIR%\%1\html\index.html
)
exit /b %ERRORLEVEL%
