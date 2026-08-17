# JUCE Frame Rate Bench

An apples-to-apples GUI responsiveness benchmark for JUCE, used to prove that
JUCE-on-QNX renders at least as smoothly as Ubuntu / Raspberry Pi OS on the
**same RPi 4/5 hardware**, and to A/B the QNX present paths.

One binary covers **both graphics modes** — the CPU/software present path and
the OpenGL (EGL) path — selected at runtime with `BENCH_RENDERER`. That matters
because SurgeXT on QNX defaults to the OpenGL path, so a software-only number
would not describe what SurgeXT actually does.

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
| `BENCH_RENDERER` | `software` | `software` = CPU render + platform blit/post; `opengl` = OpenGLContext with component painting + continuous repainting (EGL swap on QNX) |

`full` stresses raw fill-rate. `partial` isolates the **dirty-region** win: on a
backend that ignores the dirty rect, `partial` costs the same as `full`; on one
that honours it, `partial` collapses to near-zero render cost.

### The two graphics modes

| `BENCH_RENDERER` | What it exercises | QNX specifics |
|---|---|---|
| `software` | JUCE renders into a CPU image, the platform blits and posts it | Screen path; `JUCE_QNX_FAST_PRESENT=1` enables the dirty-region optimisation |
| `opengl` | Component is painted into a GL texture and shown via a buffer swap | Sets the peer property `juce_qnx_use_opengl_presentation` so the peer stops allocating software Screen buffers; presents via `eglSwapBuffers` |

`opengl` mode is exactly the SurgeXT-on-QNX configuration, so its numbers
transfer to SurgeXT directly. `BENCH_MODE=partial` is ignored in `opengl` mode
(continuous repainting redraws the whole component); the bench logs a
`BENCH_NOTE` when it does that rather than silently reporting a run that was not
what the tag claims.

With `JUCE_QNX_LOG_FPS=1`, **both** paths emit the same `QNX_PRESENT_FPS` line —
the software path from `juce_Windowing_qnx.cpp`, the GL path from
`juce_OpenGL_qnx.h` — distinguished by `mode=fast|full|opengl`. This is the
independent cross-check on the app-side `FRAMEBENCH` number, and it works for
any JUCE app including SurgeXT with no app code changes.

## Build

**QNX (aarch64) — cross-compiles from Windows or Linux/WSL, no Pi needed:**
```cmd
REM Windows (the QNX SDP install is dual-host, so q++ runs natively here)
extras\FrameRateBenchuild_frame_rate_bench_qnx.bat
```
```bash
# Linux / WSL
extras/FrameRateBench/build_frame_rate_bench_qnx.sh
# -> build/frame_rate_bench/12.2.0_gcc_ntoaarch64le/JUCEFrameRateBench
```

Both scripts build `juce_opengl` and link `-lEGL -lGLESv2`, so the single
deployed binary serves both renderers.

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

For unattended deploys set the password in `FRB_SSH_PASSWORD` (or point
`--password-env` elsewhere); `--no-prompt` skips the prompt entirely and uses the
SSH key/agent.

## Run the whole matrix in one go (recommended)

`deploy.py` also ships `run_bench_matrix.sh`. Run it **from a terminal on the
target** — a Screen/X11 GUI app generally will not attach to the display over a
bare SSH session:

```bash
cd ~/framerate-bench && ./run_bench_matrix.sh          # 20s per run
SECONDS_PER_RUN=30 COMPLEXITY=4000 ./run_bench_matrix.sh
```

It picks its matrix from `uname`, so QNX and Linux logs come from identical
steps: on QNX it runs software-baseline, software-fast, opengl, plus the two
`partial` dirty-region probes; on Linux, software, opengl and software-partial.

Then pull the log and tabulate it:

```bash
python deploy.py --target root@<pi> --no-deploy --pull-log
python summarize_results.py FrameRateBench-<host>-<timestamp>.log
```

`summarize_results.py` accepts several logs at once, so the QNX and Ubuntu runs
tabulate side by side. It drops each run's first second (window creation, first
paint, EGL setup) as warm-up rather than letting it drag the averages down.

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
  SurgeXT can be measured identically on Ubuntu. (On QNX both the software and
  GL paths now emit it.)
- Double-buffering is a separate axis from dirty-region; the prototype keeps the
  single native buffer. Add `screen_create_window_buffers(win, 2)` behind its own
  toggle once dirty-region numbers are in.
- The JUCE edits live on the `qnx-ports` branch of the JUCE submodule; propagate
  to the surge / GENISYS submodule pointers to measure those apps.
