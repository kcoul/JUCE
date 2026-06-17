#!/usr/bin/env bash
# Build the Frame Rate Bench for QNX (software present path).
# Mirrors extras/QNXDesktopWindowDemo/build_qnx_desktop_window_demo.sh.
set -euo pipefail

source "$HOME/qnx800/qnxsdp-env.sh"

TARGET="${1:-12.2.0,gcc_ntoaarch64le}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET_DIR="${TARGET//,/_}"
BUILD_DIR="$ROOT/build/frame_rate_bench/$TARGET_DIR"
GENERATED_HEADER="$BUILD_DIR/GeneratedBuildVersion.h"

mkdir -p "$BUILD_DIR"

cmake -DROOT="$ROOT" -DOUTPUT_HEADER="$GENERATED_HEADER" -P "$ROOT/extras/FrameRateBench/cmake/GenerateBuildVersion.cmake"

COMMON=(
  "-V${TARGET}"
  -std=gnu++17
  -O2
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  -DJUCE_USE_CURL=0
  -DJUCE_WEB_BROWSER=0
  -DJUCE_JACK=0
  -DJUCE_USE_FONTCONFIG=0
  "-I$ROOT"
  "-I$ROOT/modules"
  "-I$BUILD_DIR"
)

q++ "${COMMON[@]}" -c "$ROOT/modules/juce_core/juce_core.cpp" -o "$BUILD_DIR/juce_core.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_core/juce_core_CompilationTime.cpp" -o "$BUILD_DIR/juce_core_CompilationTime.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_events/juce_events.cpp" -o "$BUILD_DIR/juce_events.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_graphics/juce_graphics.cpp" -o "$BUILD_DIR/juce_graphics.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_graphics/juce_graphics_Harfbuzz.cpp" -o "$BUILD_DIR/juce_graphics_Harfbuzz.o"
qcc "-V${TARGET}" -DSB_CONFIG_UNITY=1 -I"$ROOT" -I"$ROOT/modules" -c "$ROOT/modules/juce_graphics/unicode/sheenbidi/Source/SheenBidi.c" -o "$BUILD_DIR/SheenBidi.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_data_structures/juce_data_structures.cpp" -o "$BUILD_DIR/juce_data_structures.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics.cpp" -o "$BUILD_DIR/juce_gui_basics.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics_2.cpp" -o "$BUILD_DIR/juce_gui_basics_2.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics_3.cpp" -o "$BUILD_DIR/juce_gui_basics_3.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics_4.cpp" -o "$BUILD_DIR/juce_gui_basics_4.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics_5.cpp" -o "$BUILD_DIR/juce_gui_basics_5.o"
q++ "${COMMON[@]}" -c "$ROOT/extras/FrameRateBench/Source/Main.cpp" -o "$BUILD_DIR/Main.o"

q++ "-V${TARGET}" \
  "$BUILD_DIR/juce_core.o" \
  "$BUILD_DIR/juce_core_CompilationTime.o" \
  "$BUILD_DIR/juce_events.o" \
  "$BUILD_DIR/juce_graphics.o" \
  "$BUILD_DIR/juce_graphics_Harfbuzz.o" \
  "$BUILD_DIR/SheenBidi.o" \
  "$BUILD_DIR/juce_data_structures.o" \
  "$BUILD_DIR/juce_gui_basics.o" \
  "$BUILD_DIR/juce_gui_basics_2.o" \
  "$BUILD_DIR/juce_gui_basics_3.o" \
  "$BUILD_DIR/juce_gui_basics_4.o" \
  "$BUILD_DIR/juce_gui_basics_5.o" \
  "$BUILD_DIR/Main.o" \
  -lscreen -lsocket -lz -lexpat \
  -o "$BUILD_DIR/JUCEFrameRateBench"

echo "Built: $BUILD_DIR/JUCEFrameRateBench"
