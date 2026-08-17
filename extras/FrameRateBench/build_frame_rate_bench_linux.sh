#!/usr/bin/env bash
# Build the Frame Rate Bench natively on Ubuntu / Raspberry Pi OS (the comparison
# side of the apples-to-apples test). Run this ON the Pi (aarch64), same hardware
# you boot QNX on. JUCE pulls the X11/freetype deps via its modules.
#
#   sudo apt install build-essential cmake libx11-dev libxext-dev libxinerama-dev \
#        libxrandr-dev libxcursor-dev libfreetype6-dev libasound2-dev
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT/build/frame_rate_bench/linux-$(uname -m)"

cmake -S "$ROOT/extras/FrameRateBench" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel

echo "Built: $BUILD_DIR/JUCEFrameRateBench"
