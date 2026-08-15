"""
summarize_results.py
Turn one or more FrameRateBench logs into a comparison table.

    python summarize_results.py FrameRateBench-10.163.142.184-20260814.log
    python summarize_results.py qnx.log ubuntu.log

Parses three line kinds the bench emits:
  BENCH_CONFIG renderer=.. mode=.. complexity=.. seconds=.. os=.. cpus=..
  FRAMEBENCH tag=.. fps=.. frames=.. windowMs=.. meanMs=.. p50Ms=.. p95Ms=.. jank=..
  QNX_PRESENT_FPS fps=.. frames=.. windowMs=.. mode=fast|full|opengl

FRAMEBENCH is the app-side rate (frames the bench actually rendered).
QNX_PRESENT_FPS is what the platform present/swap path achieved. They should
agree closely; a large gap means frames are being rendered and dropped, or
presented without a repaint behind them, so both are reported.

The first report of each run is dropped: it covers window creation, first paint
and (in opengl mode) EGL context setup, none of which is steady-state.
"""

import re
import sys
from collections import OrderedDict


def _fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def _median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return 0.0
    mid = n // 2
    return s[mid] if n % 2 else (s[mid - 1] + s[mid]) / 2.0


def parse(path):
    """Return a list of runs, each a dict of config + aggregated samples."""
    runs = []
    current = None

    with open(path, 'r', errors='replace') as handle:
        for line in handle:
            if 'BENCH_CONFIG' in line:
                f = _fields(line)
                current = {
                    'renderer': f.get('renderer', '?'),
                    'mode': f.get('mode', '?'),
                    'complexity': f.get('complexity', '?'),
                    'os': f.get('os', '?'),
                    'window': None,
                    'tag': None,
                    'app_fps': [],
                    'mean_ms': [],
                    'p95_ms': [],
                    'jank': [],
                    'present_fps': [],
                    'present_mode': None,
                }
                runs.append(current)

            elif 'BENCH_WINDOW' in line and current is not None:
                f = _fields(line)
                current['window'] = f"{f.get('width', '?')}x{f.get('height', '?')}"

            elif 'FRAMEBENCH' in line and current is not None:
                f = _fields(line)
                current['tag'] = current['tag'] or f.get('tag')
                try:
                    current['app_fps'].append(float(f['fps']))
                    current['mean_ms'].append(float(f['meanMs']))
                    current['p95_ms'].append(float(f['p95Ms']))
                    current['jank'].append(int(f.get('jank', 0)))
                except (KeyError, ValueError):
                    pass

            elif 'QNX_PRESENT_FPS' in line and current is not None:
                f = _fields(line)
                current['present_mode'] = f.get('mode', current['present_mode'])
                try:
                    current['present_fps'].append(float(f['fps']))
                except (KeyError, ValueError):
                    pass

    # Drop each run's first (warm-up) sample and any run that never got going.
    cleaned = []
    for r in runs:
        for key in ('app_fps', 'mean_ms', 'p95_ms', 'present_fps', 'jank'):
            if len(r[key]) > 1:
                r[key] = r[key][1:]
        if r['app_fps']:
            cleaned.append(r)

    return cleaned


def main():
    paths = sys.argv[1:]
    if not paths:
        raise SystemExit(__doc__)

    rows = []
    for path in paths:
        for r in parse(path):
            rows.append((path, r))

    if not rows:
        raise SystemExit("No complete runs found. Does the log contain BENCH_CONFIG "
                         "and FRAMEBENCH lines? (Older builds logged neither.)")

    header = ("tag", "os", "renderer", "mode", "cplx", "window", "fps(med)",
              "fps(min)", "mean ms", "p95 ms", "jank", "present fps")
    widths = [16, 9, 9, 8, 6, 10, 9, 9, 8, 8, 6, 12]

    def emit(cells):
        print("  ".join(str(c).ljust(w) for c, w in zip(cells, widths)).rstrip())

    emit(header)
    emit(["-" * w for w in widths])

    for _, r in rows:
        present = _median(r['present_fps']) if r['present_fps'] else None
        present_txt = "-"
        if present is not None:
            present_txt = "%.1f (%s)" % (present, r['present_mode'] or "?")

        emit([
            r['tag'] or "?",
            r['os'][:9],
            r['renderer'],
            r['mode'],
            r['complexity'],
            r['window'] or "?",
            "%.1f" % _median(r['app_fps']),
            "%.1f" % min(r['app_fps']),
            "%.1f" % _median(r['mean_ms']),
            "%.1f" % _median(r['p95_ms']),
            sum(r['jank']),
            present_txt,
        ])

    # Parity check: same configuration on different operating systems. This is
    # the question the harness exists to answer, so it is computed rather than
    # eyeballed off the table.
    by_config = OrderedDict()
    for _, r in rows:
        key = (r['renderer'], r['mode'], r['complexity'])
        by_config.setdefault(key, {})[r['os']] = _median(r['app_fps'])

    cross = [(k, v) for k, v in by_config.items() if len(v) > 1]
    if cross:
        print("")
        print("Parity (same renderer/mode/complexity across operating systems):")
        oses = []
        for _, v in cross:
            for name in v:
                if name not in oses:
                    oses.append(name)

        head = ["renderer", "mode", "cplx"] + oses + ["ratio"]
        w = [9, 8, 6] + [10] * len(oses) + [16]
        print("  ".join(str(c).ljust(x) for c, x in zip(head, w)).rstrip())
        print("  ".join("-" * x for x in w))

        for (renderer, mode, cplx), v in cross:
            cells = [renderer, mode, cplx] + ["%.1f" % v[o] if o in v else "-" for o in oses]
            vals = [v[o] for o in oses if o in v]
            if len(vals) == 2 and min(vals) > 0:
                lead = oses[0] if vals[0] > vals[1] else oses[1]
                cells.append("%s +%.0f%%" % (lead, 100.0 * (max(vals) / min(vals) - 1.0)))
            else:
                cells.append("-")
            print("  ".join(str(c).ljust(x) for c, x in zip(cells, w)).rstrip())

    # The headline comparison: software vs opengl, per OS, at equal complexity.
    print("")
    by_os = OrderedDict()
    for _, r in rows:
        by_os.setdefault(r['os'], []).append(r)

    for os_name, os_runs in by_os.items():
        full = [r for r in os_runs if r['mode'] != 'partial']
        sw = [r for r in full if r['renderer'] == 'software']
        gl = [r for r in full if r['renderer'] == 'opengl']
        if sw and gl:
            best_sw = max(_median(r['app_fps']) for r in sw)
            best_gl = max(_median(r['app_fps']) for r in gl)
            faster = "opengl" if best_gl > best_sw else "software"
            ratio = max(best_gl, best_sw) / max(1e-9, min(best_gl, best_sw))
            print("%s: software %.1f fps vs opengl %.1f fps -> %s is %.2fx faster"
                  % (os_name, best_sw, best_gl, faster, ratio))


if __name__ == "__main__":
    main()
