#!/usr/bin/env bash
set -euo pipefail

source /Users/kicoulter/qnx800/qnxsdp-env.sh

TARGET="${1:-12.2.0,gcc_ntoaarch64le}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../.. && pwd)"
TARGET_DIR="${TARGET//,/_}"
BUILD_DIR="$ROOT/build/qnx_hello_world/$TARGET_DIR"

mkdir -p "$BUILD_DIR"

COMMON=(
  "-V${TARGET}"
  -std=gnu++17
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  -DJUCE_USE_CURL=0
  -DJUCE_WEB_BROWSER=0
  -DJUCE_JACK=0
  -DJUCE_ALSA=1
  -DJUCE_USE_FONTCONFIG=0
  -I"$ROOT"
  -I"$ROOT/modules"
)

q++ "${COMMON[@]}" -c "$ROOT/modules/juce_core/juce_core.cpp" -o "$BUILD_DIR/juce_core.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_core/juce_core_CompilationTime.cpp" -o "$BUILD_DIR/juce_core_CompilationTime.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_events/juce_events.cpp" -o "$BUILD_DIR/juce_events.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_graphics/juce_graphics.cpp" -o "$BUILD_DIR/juce_graphics.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_graphics/juce_graphics_Harfbuzz.cpp" -o "$BUILD_DIR/juce_graphics_Harfbuzz.o"
qcc "-V${TARGET}" -DSB_CONFIG_UNITY=1 -I"$ROOT" -I"$ROOT/modules" -c "$ROOT/modules/juce_graphics/unicode/sheenbidi/Source/SheenBidi.c" -o "$BUILD_DIR/SheenBidi.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_data_structures/juce_data_structures.cpp" -o "$BUILD_DIR/juce_data_structures.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_basics/juce_gui_basics.cpp" -o "$BUILD_DIR/juce_gui_basics.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_gui_extra/juce_gui_extra.cpp" -o "$BUILD_DIR/juce_gui_extra.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_audio_basics/juce_audio_basics.cpp" -o "$BUILD_DIR/juce_audio_basics.o"
q++ "${COMMON[@]}" -c "$ROOT/modules/juce_audio_devices/juce_audio_devices.cpp" -o "$BUILD_DIR/juce_audio_devices.o"
q++ "${COMMON[@]}" -c "$ROOT/extras/QNXHelloWorld/Source/Main.cpp" -o "$BUILD_DIR/Main.o"

q++ "-V${TARGET}" \
  "$BUILD_DIR/juce_core.o" \
  "$BUILD_DIR/juce_core_CompilationTime.o" \
  "$BUILD_DIR/juce_events.o" \
  "$BUILD_DIR/juce_graphics.o" \
  "$BUILD_DIR/juce_graphics_Harfbuzz.o" \
  "$BUILD_DIR/SheenBidi.o" \
  "$BUILD_DIR/juce_data_structures.o" \
  "$BUILD_DIR/juce_gui_basics.o" \
  "$BUILD_DIR/juce_gui_extra.o" \
  "$BUILD_DIR/juce_audio_basics.o" \
  "$BUILD_DIR/juce_audio_devices.o" \
  "$BUILD_DIR/Main.o" \
  -lscreen -lasound -lsocket -lz -lexpat \
  -o "$BUILD_DIR/JUCEQNXHelloWorld"

echo "Built: $BUILD_DIR/JUCEQNXHelloWorld"
