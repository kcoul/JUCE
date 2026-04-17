@echo off
setlocal

set ROOT=%~dp0..\..
for %%I in ("%ROOT%") do set ROOT=%%~fI

set BUILD_DIR=%ROOT%\build\windows_midi_to_osc_bridge
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -DJUCE_BUILD_EXTRAS=ON || exit /b 1
cmake --build "%BUILD_DIR%" --target MidiToOscBridge --config Release || exit /b 1

echo Built: %BUILD_DIR%\extras\MidiToOscBridge\MidiToOscBridge_artefacts\Release\JUCE MIDI To OSC Bridge.exe
