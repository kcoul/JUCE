# JUCE Frame Rate Bench

An apples-to-apples GUI responsiveness benchmark for JUCE, used to prove that
JUCE-on-QNX renders at least as smoothly as Ubuntu / Raspberry Pi OS on the
**same RPi 4/5 hardware**, and to A/B the QNX software-present optimisations.

## What it measures

`FrameRateMonitor.h` (portable, header-only) records one sample per presented
frame and, once per second, emits a single greppable line:

```
FRAMEBENCH tag=SW-QNX fps=58.3 frames=58 windowMs=994.8 meanMs=17.1 minMs=15.9 maxMs=33.2 p50Ms=16.8 p95Ms=22.4 p99Ms=31.0 jank=2
```

`fps` is throughput; the percentiles are what the user *feels*. A run that holds
60 fps but spikes to `p99Ms=31` will feel janky — that is the number to watch,
not just the average.

## Two vehicles (both compare the same way)

1. **This synthetic app** — a deterministic, CPU-render-bound scene. Identical
   workload on every OS, no SurgeXT variables. Best for clean comparison and for
   sweeping load to find where each OS drops below 60 fps.
2. **SurgeXT itself** — set `JUCE_QNX_LOG_FPS=1` and the QNX windowing backend
   emits `QNX_PRESENT_FPS ...` lines from its present path with **no app code
   changes**. (The Linux backend needs the equivalent hook added for the Ubuntu
   side — see "Pending parity" below.)

## Configuration (env vars — runs are scriptable)

| Var | Default | Meaning |
|---|---|---|
| `BENCH_TAG` | `SW` | Log tag, e.g. `SW-QNX` / `SW-UBUNTU` |
| `BENCH_SECONDS` | `0` | Auto-quit after N seconds (0 = until closed) |
| `BENCH_MODE` | `full` | `full` = invalidate whole window; `partial` = small moving rect |
| `BENCH_COMPLEXITY` | `1500` | Draw ops/frame; sweep to find the 60 fps cliff |
| `BENCH_WIDTH`/`BENCH_HEIGHT` | display size | Window size |

`full` stresses raw fill-rate. `partial` isolates the **dirty-region** win: on a
backend that ignores the dirty rect, `partial` costs the same as `full`; on one
that honours it, `partial` collapses to near-zero render cost.

## Build

**QNX (aarch64):**
```bash
extras/FrameRateBench/build_frame_rate_bench_qnx.sh
# -> build/frame_rate_bench/12.2.0_gcc_ntoaarch64le/JUCEFrameRateBench
```

**Ubuntu / Raspberry Pi OS (run on the Pi, aarch64):**
```bash
extras/FrameRateBench/build_frame_rate_bench_linux.sh
# -> build/frame_rate_bench/linux-aarch64/JUCEFrameRateBench
```

## Deploy to the QNX target (Windows/WSL -> QNX)

`deploy.py` SFTPs the binary to the target (paramiko; same script works from
Windows or WSL). Only the binary ships — `screen/socket/z/expat/freetype` are QNX
system libs already on the target image.

```bash
python deploy.py --target root@192.168.1.50
# build + run a 20s fast-present partial sweep in one go:
python deploy.py --target root@192.168.1.50 --run \
    --env JUCE_QNX_FAST_PRESENT=1 --env JUCE_QNX_LOG_FPS=1 \
    --env BENCH_MODE=partial --env BENCH_SECONDS=20 --env BENCH_TAG=SW-QNX
```

It best-effort `slay`s a running instance first (SFTP can't overwrite a running
binary). `--run` backgrounds the app over SSH; if the Screen display won't attach
over SSH, drop `--run` and launch from a terminal on the target instead.

## Run the comparison

Same hardware, boot QNX then Ubuntu (or swap SD cards). For each OS, sweep load:

```bash
for c in 500 1500 4000 8000; do
  BENCH_TAG=SW-QNX BENCH_SECONDS=20 BENCH_MODE=full BENCH_COMPLEXITY=$c ./JUCEFrameRateBench
done
```

Repeat with `BENCH_TAG=SW-UBUNTU` on the Ubuntu build. Collect the
`FrameRateBench.log` files (next to each binary) and diff the `FRAMEBENCH` lines.
QNX should match or beat Ubuntu at every complexity level — that is the claim we
are trying to substantiate.

## A/B the QNX optimisation

The QNX software-present path has a prototype dirty-region + persistent-image
mode behind an env toggle (`JUCE_QNX_FAST_PRESENT`, in
`modules/juce_gui_basics/native/juce_Windowing_qnx.cpp`):

```bash
# baseline (full-window re-render every frame)
JUCE_QNX_FAST_PRESENT=0 BENCH_MODE=partial BENCH_COMPLEXITY=4000 ./JUCEFrameRateBench
# prototype (render/blit/post only the dirty rect, reuse backing image)
JUCE_QNX_FAST_PRESENT=1 BENCH_MODE=partial BENCH_COMPLEXITY=4000 ./JUCEFrameRateBench
```

In `partial` mode the prototype should show a large fps/percentile improvement;
in `full` mode it should be a wash (the win is dirty-region, and full mode has no
small dirty region to exploit). Use `JUCE_QNX_LOG_FPS=1` to also get
`QNX_PRESENT_FPS` lines straight from the present path (works for SurgeXT too).

## Pending parity (next steps)

- Add a `QNX_PRESENT_FPS`-equivalent present-rate log to the Linux/X11 backend so
  SurgeXT can be measured identically on Ubuntu.
- Double-buffering is a separate axis from dirty-region; the prototype keeps the
  single native buffer. Add `screen_create_window_buffers(win, 2)` behind its own
  toggle once dirty-region numbers are in.
- The JUCE edits live on the `qnx-ports` branch of the JUCE submodule; propagate
  to the surge / GENISYS submodule pointers to measure those apps.
