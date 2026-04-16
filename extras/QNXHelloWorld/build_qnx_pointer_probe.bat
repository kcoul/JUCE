@echo off
setlocal

call C:\Users\kicoulter\qnx800\qnxsdp-env.bat
if errorlevel 1 exit /b %errorlevel%

set TARGET=%1
if "%TARGET%"=="" set TARGET=12.2.0,gcc_ntoaarch64le

set ROOT=%~dp0..\..
for %%I in ("%ROOT%") do set ROOT=%%~fI
set TARGET_DIR=%TARGET:,=_%
set BUILD_DIR=%ROOT%\build\qnx_pointer_probe\%TARGET_DIR%
set GENERATED_HEADER=%BUILD_DIR%\GeneratedBuildVersion.h

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -DROOT="%ROOT%" -DOUTPUT_HEADER="%GENERATED_HEADER%" -P "%ROOT%\extras\QNXHelloWorld\cmake\GenerateBuildVersion.cmake" || exit /b 1

q++ -V%TARGET% -std=gnu++17 -I"%BUILD_DIR%" ^
    -include "%GENERATED_HEADER%" ^
    -c "%ROOT%\extras\QNXHelloWorld\Source\PointerProbe.cpp" ^
    -o "%BUILD_DIR%\PointerProbe.o" || exit /b 1

q++ -V%TARGET% ^
    "%BUILD_DIR%\PointerProbe.o" ^
    -lscreen ^
    -o "%BUILD_DIR%\QNXPointerProbe" || exit /b 1

echo Built: %BUILD_DIR%\QNXPointerProbe
