#!/bin/sh
# Sweep render load across both graphics modes.
#
# A single complexity cannot separate the present paths: pick one that saturates
# the rasterizer and every mode reports the same number (they are all waiting on
# drawing, not on presentation); pick one that is too light and everything sits
# at the vsync cap. Sweeping shows both ends — where each mode leaves 60 fps, and
# what its throughput ceiling is.
#
# Run this ON THE TARGET. Same script on QNX and Linux, so the two sweeps are
# directly comparable.
#
#   ./run_bench_sweep.sh
#   COMPLEXITIES="100 500 2000" SECONDS_PER_RUN=15 ./run_bench_sweep.sh

set -u

BIN=${BIN:-./JUCEFrameRateBench}
SECONDS_PER_RUN=${SECONDS_PER_RUN:-15}
COMPLEXITIES=${COMPLEXITIES:-"100 400 1500 4000"}
# Pinned so both OSes render the same pixel count. QNX's panel happens to be
# 1280x1024; an X server would otherwise use the monitor's native mode and the
# two sides would not be comparable. BENCH_WINDOW in the log records what was
# actually obtained, so this can be checked rather than assumed.
BENCH_WIDTH=${BENCH_WIDTH:-1280}
BENCH_HEIGHT=${BENCH_HEIGHT:-1024}
export BENCH_WIDTH BENCH_HEIGHT

if [ ! -x "$BIN" ]; then
    echo "Not found or not executable: $BIN" >&2
    exit 1
fi

OS=$(uname -s)

# On QNX the app talks to Screen directly. On Linux JUCE needs an X server (it
# has no Wayland or DRM backend), so bring up a bare Xorg — no window manager,
# no compositor — for the whole sweep. One server for all runs, rather than
# xinit per run, keeps startup cost out of the measurements.
XPID=""
if [ "$OS" != "QNX" ] && [ -z "${DISPLAY:-}" ]; then
    echo "Starting bare X server on :0 (no window manager)"
    X :0 vt1 >/tmp/bench-xorg.log 2>&1 &
    XPID=$!

    i=0
    while [ $i -lt 15 ] && [ ! -e /tmp/.X11-unix/X0 ]; do
        i=$((i + 1))
        sleep 1
    done

    if [ ! -e /tmp/.X11-unix/X0 ]; then
        echo "X server failed to start; see /tmp/bench-xorg.log" >&2
        exit 1
    fi

    DISPLAY=:0
    export DISPLAY
fi

cleanup() {
    [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
}
trap cleanup EXIT INT TERM

if [ "$OS" = "QNX" ]; then
    # Software here means the optimised present path; the unoptimised baseline is
    # covered by run_bench_matrix.sh and does not need re-measuring per complexity.
    MODES="software opengl"
else
    MODES="software opengl"
fi

echo "Sweep on $OS: modes [$MODES] x complexity [$COMPLEXITIES], ${SECONDS_PER_RUN}s each"

for cplx in $COMPLEXITIES; do
    for mode in $MODES; do
        tag="${mode}-${cplx}-${OS}"
        echo ""
        echo "=== $tag ==="

        if [ "$OS" = "QNX" ] && [ "$mode" = "software" ]; then
            env BENCH_TAG="$tag" BENCH_SECONDS="$SECONDS_PER_RUN" \
                BENCH_COMPLEXITY="$cplx" BENCH_RENDERER=software \
                JUCE_QNX_FAST_PRESENT=1 JUCE_QNX_LOG_FPS=1 "$BIN"
        else
            env BENCH_TAG="$tag" BENCH_SECONDS="$SECONDS_PER_RUN" \
                BENCH_COMPLEXITY="$cplx" BENCH_RENDERER="$mode" \
                JUCE_QNX_LOG_FPS=1 "$BIN"
        fi

        sleep 2
    done
done

echo ""
echo "Done. Results appended to $(dirname "$BIN")/FrameRateBench.log"
