#!/usr/bin/env bash
# Cross-build the Frame Rate Bench for aarch64 Linux (Raspberry Pi OS / Ubuntu)
# from an x86_64 host — in practice, from WSL on the same laptop that does the
# QNX cross-build. Avoids needing a toolchain, source tree and apt dependencies
# on the Pi itself, and keeps both sides of the A/B built from one checkout.
#
#   ./build_frame_rate_bench_linux_cross.sh
#   -> build/frame_rate_bench/linux-aarch64-cross/JUCEFrameRateBench
#
# One-time host setup is documented in cmake/aarch64-linux-gnu.cmake. Verify the
# result really is aarch64 before shipping it:  file <binary>
#
# To build natively ON the Pi instead, use build_frame_rate_bench_linux.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT/build/frame_rate_bench/linux-aarch64-cross${AARCH64_GCC_SUFFIX:-}"
TOOLCHAIN="$ROOT/extras/FrameRateBench/cmake/aarch64-linux-gnu.cmake"

if ! command -v "aarch64-linux-gnu-g++${AARCH64_GCC_SUFFIX:-}" >/dev/null 2>&1; then
    echo "aarch64-linux-gnu-g++ not found. Install it with:" >&2
    echo "  sudo apt install g++-aarch64-linux-gnu" >&2
    exit 1
fi

# pkg-config has to look at arm64 .pc files for the configure step too, not just
# inside CMake, or JUCE's module dependency probing resolves against the host.
export PKG_CONFIG_LIBDIR="/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig"

cmake -S "$ROOT/extras/FrameRateBench" -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
      -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --parallel

BINARY="$BUILD_DIR/JUCEFrameRateBench"
echo ""
echo "Built: $BINARY"
file "$BINARY" || true
