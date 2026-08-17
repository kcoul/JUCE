#!/bin/sh
# Run the full frame-rate comparison matrix and leave one log behind.
#
# Run this ON THE TARGET, from a terminal attached to the display — a Screen
# (QNX) or X11 (Linux) GUI app generally will not come up over a bare SSH
# session. The same script serves both OSes: it picks the matrix from `uname`,
# so the QNX and Linux logs are produced by identical steps.
#
#   ./run_bench_matrix.sh                 # 20s per run, complexity 1500
#   SECONDS_PER_RUN=30 COMPLEXITY=4000 ./run_bench_matrix.sh
#
# Every run appends to FrameRateBench.log next to the binary. Pull it with
#   python deploy.py --target root@<pi> --no-deploy --pull-log
# and turn it into a table with
#   python summarize_results.py FrameRateBench-<host>-<ts>.log

set -u

BIN=${BIN:-./JUCEFrameRateBench}
SECONDS_PER_RUN=${SECONDS_PER_RUN:-20}
COMPLEXITY=${COMPLEXITY:-1500}

if [ ! -x "$BIN" ]; then
    echo "Not found or not executable: $BIN" >&2
    exit 1
fi

OS=$(uname -s)

# On QNX the app talks to Screen directly. On Linux JUCE needs an X server (it
# has no Wayland or DRM backend), so bring up a bare Xorg — no window manager,
# no compositor — for the whole matrix.
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

# One run = one configuration. Args: tag, then KEY=VAL env pairs.
run_case() {
    tag=$1
    shift

    echo ""
    echo "=== $tag  (${SECONDS_PER_RUN}s, complexity ${COMPLEXITY}) ==="

    # JUCE_QNX_LOG_FPS makes the platform present/swap path report its own rate,
    # which cross-checks the app-side FRAMEBENCH number. It is inert on Linux.
    env BENCH_TAG="$tag" \
        BENCH_SECONDS="$SECONDS_PER_RUN" \
        BENCH_COMPLEXITY="$COMPLEXITY" \
        JUCE_QNX_LOG_FPS=1 \
        "$@" \
        "$BIN"

    # Let the window and its Screen/EGL resources go away before the next run.
    sleep 2
}

echo "Frame-rate matrix on $OS, ${SECONDS_PER_RUN}s per run, complexity ${COMPLEXITY}"

if [ "$OS" = "QNX" ]; then
    # The three QNX paths worth comparing, in increasing order of expected FPS.
    run_case SW-BASE-QNX BENCH_RENDERER=software JUCE_QNX_FAST_PRESENT=0
    run_case SW-FAST-QNX BENCH_RENDERER=software JUCE_QNX_FAST_PRESENT=1
    run_case GL-QNX      BENCH_RENDERER=opengl

    # Dirty-region A/B: on a backend that ignores the dirty rect these two cost
    # the same as the full-window runs above; on one that honours it, the fast
    # case collapses to near-zero render cost.
    run_case SW-BASE-PARTIAL-QNX BENCH_RENDERER=software BENCH_MODE=partial JUCE_QNX_FAST_PRESENT=0
    run_case SW-FAST-PARTIAL-QNX BENCH_RENDERER=software BENCH_MODE=partial JUCE_QNX_FAST_PRESENT=1
else
    # Linux comparison side: same workload, same two renderers.
    run_case SW-LINUX BENCH_RENDERER=software
    run_case GL-LINUX BENCH_RENDERER=opengl
    run_case SW-PARTIAL-LINUX BENCH_RENDERER=software BENCH_MODE=partial
fi

echo ""
echo "Done. Results appended to $(dirname "$BIN")/FrameRateBench.log"
echo "Summary of this session:"
grep -E 'BENCH_CONFIG|FRAMEBENCH|QNX_PRESENT_FPS' "$(dirname "$BIN")/FrameRateBench.log" | tail -40
