@echo off
setlocal

call C:\Users\kicoulter\qnx800\qnxsdp-env.bat
if errorlevel 1 exit /b %errorlevel%

set TARGET=%1
if "%TARGET%"=="" set TARGET=12.2.0,gcc_ntoaarch64le

set ROOT=%~dp0..\..
for %%I in ("%ROOT%") do set ROOT=%%~fI
set TARGET_DIR=%TARGET:,=_%
set BUILD_DIR=%ROOT%\build\qnx_touch_fullscreen_demo\%TARGET_DIR%
set GENERATED_HEADER=%BUILD_DIR%\GeneratedBuildVersion.h
set FREETYPE_LIB=%QNX_TARGET%/aarch64le/usr/lib/libfreetype.so.24

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -DROOT="%ROOT%" -DOUTPUT_HEADER="%GENERATED_HEADER%" -P "%ROOT%\extras\QNXTouchFullscreenDemo\cmake\GenerateBuildVersion.cmake" || exit /b 1

set COMMON=-V%TARGET% -std=gnu++17 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_JACK=0 -DJUCE_ALSA=1 -DJUCE_USE_FONTCONFIG=0 -I"%ROOT%" -I"%ROOT%\modules" -I"%BUILD_DIR%" -I"%QNX_TARGET%/usr/include/freetype2"

q++ %COMMON% -c "%ROOT%\modules\juce_core\juce_core.cpp" -o "%BUILD_DIR%\juce_core.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_core\juce_core_CompilationTime.cpp" -o "%BUILD_DIR%\juce_core_CompilationTime.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_events\juce_events.cpp" -o "%BUILD_DIR%\juce_events.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics.cpp" -o "%BUILD_DIR%\juce_graphics.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_Harfbuzz.cpp" -o "%BUILD_DIR%\juce_graphics_Harfbuzz.o" || exit /b 1
REM JUCE 9 moved the vendored third-party C code (zlib, libpng, libjpg, lunasvg,
REM SheenBidi) out of the module unity .cpp files and into these per-dependency
REM .c unity files at each module root. They must each be compiled and linked.
set CCOMMON=-V%TARGET% -O2 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -I"%ROOT%" -I"%ROOT%\modules"

qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_Sheenbidi.c" -o "%BUILD_DIR%\juce_graphics_Sheenbidi.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_core\juce_core_zlib.c" -o "%BUILD_DIR%\juce_core_zlib.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_libpng.c" -o "%BUILD_DIR%\juce_graphics_libpng.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_libjpg_1.c" -o "%BUILD_DIR%\juce_graphics_libjpg_1.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_libjpg_2.c" -o "%BUILD_DIR%\juce_graphics_libjpg_2.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_libjpg_3.c" -o "%BUILD_DIR%\juce_graphics_libjpg_3.o" || exit /b 1
qcc %CCOMMON% -c "%ROOT%\modules\juce_graphics\juce_graphics_lunasvg.c" -o "%BUILD_DIR%\juce_graphics_lunasvg.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_data_structures\juce_data_structures.cpp" -o "%BUILD_DIR%\juce_data_structures.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_gui_basics\juce_gui_basics.cpp" -o "%BUILD_DIR%\juce_gui_basics.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_gui_basics\juce_gui_basics_2.cpp" -o "%BUILD_DIR%\juce_gui_basics_2.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_gui_basics\juce_gui_basics_3.cpp" -o "%BUILD_DIR%\juce_gui_basics_3.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_gui_basics\juce_gui_basics_4.cpp" -o "%BUILD_DIR%\juce_gui_basics_4.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_gui_basics\juce_gui_basics_5.cpp" -o "%BUILD_DIR%\juce_gui_basics_5.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_opengl\juce_opengl.cpp" -o "%BUILD_DIR%\juce_opengl.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_audio_basics\juce_audio_basics.cpp" -o "%BUILD_DIR%\juce_audio_basics.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_audio_devices\juce_audio_devices.cpp" -o "%BUILD_DIR%\juce_audio_devices.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\modules\juce_osc\juce_osc.cpp" -o "%BUILD_DIR%\juce_osc.o" || exit /b 1
q++ %COMMON% -c "%ROOT%\extras\QNXTouchFullscreenDemo\Source\Main.cpp" -o "%BUILD_DIR%\Main.o" || exit /b 1

q++ -V%TARGET% ^
    "%BUILD_DIR%\juce_core.o" ^
    "%BUILD_DIR%\juce_core_CompilationTime.o" ^
    "%BUILD_DIR%\juce_events.o" ^
    "%BUILD_DIR%\juce_graphics.o" ^
    "%BUILD_DIR%\juce_graphics_Harfbuzz.o" ^
    "%BUILD_DIR%\juce_graphics_Sheenbidi.o" ^
    "%BUILD_DIR%\juce_core_zlib.o" ^
    "%BUILD_DIR%\juce_graphics_libpng.o" ^
    "%BUILD_DIR%\juce_graphics_libjpg_1.o" ^
    "%BUILD_DIR%\juce_graphics_libjpg_2.o" ^
    "%BUILD_DIR%\juce_graphics_libjpg_3.o" ^
    "%BUILD_DIR%\juce_graphics_lunasvg.o" ^
    "%BUILD_DIR%\juce_data_structures.o" ^
    "%BUILD_DIR%\juce_gui_basics.o" ^
    "%BUILD_DIR%\juce_gui_basics_2.o" ^
    "%BUILD_DIR%\juce_gui_basics_3.o" ^
    "%BUILD_DIR%\juce_gui_basics_4.o" ^
    "%BUILD_DIR%\juce_gui_basics_5.o" ^
    "%BUILD_DIR%\juce_opengl.o" ^
    "%BUILD_DIR%\juce_audio_basics.o" ^
    "%BUILD_DIR%\juce_audio_devices.o" ^
    "%BUILD_DIR%\juce_osc.o" ^
    "%BUILD_DIR%\Main.o" ^
    -lscreen -lasound -lsocket -lEGL -lGLESv2 -lz -lexpat "%FREETYPE_LIB%" ^
    -o "%BUILD_DIR%\JUCEQNXTouchFullscreenDemo" || exit /b 1

echo Built: %BUILD_DIR%\JUCEQNXTouchFullscreenDemo
