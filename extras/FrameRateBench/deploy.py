"""
deploy.py
Deploy the Frame Rate Bench binary from a Windows (or Linux/WSL) host to a QNX
target over SFTP, mirroring the AutoDemos/VREngine deploy pattern.

    python deploy.py --target 192.168.1.50
    python deploy.py --target root@192.168.1.50 --deploy-path /data/home/root/bench
    python deploy.py --target 192.168.1.50 --run --env BENCH_SECONDS=20 --env JUCE_QNX_FAST_PRESENT=1

Auth: pass --password-env VAR (or set FRB_SSH_PASSWORD) to run unattended, e.g.
from CI or an agent session; otherwise the password is prompted for. With
--no-prompt the SSH key/agent is used and no prompt appears at all.

The run_bench_matrix.sh helper ships alongside the binary, because a Screen/X11
GUI app generally will not attach to the display over a bare SSH session — run
the matrix from a terminal on the target instead of relying on --run.

All libs the bench links against (screen, socket, z, expat, freetype) are QNX
system libs already present on the target image, so only the binary ships — no
bundled .so files needed (unlike the VREngine/ONNX deploy).

NOTE: SFTP cannot overwrite a binary that is currently executing. The script
makes a best-effort `slay` of any running instance first; if that fails, stop it
manually on the target before redeploying.
"""

import argparse
import getpass
import os

try:
    import paramiko
except ImportError:
    raise SystemExit("pip install paramiko")

_BINARY_NAME   = "JUCEFrameRateBench"
_MATRIX_SCRIPT = "run_bench_matrix.sh"
_SWEEP_SCRIPT  = "run_bench_sweep.sh"
_DEFAULT_TRIPLE = "12.2.0_gcc_ntoaarch64le"   # matches build/ layout from build_frame_rate_bench_qnx.*


_FLEET_KEY = "~/.ssh/id_ed25519_qnxpi"   # dedicated key for the QNX/Linux test boards


def _resolve_ssh_config(host, user, args):
    """Apply ~/.ssh/config to (host, user) and collect candidate key files.

    Returns (hostname, user, [key paths]). Falls back to the fleet key when the
    config says nothing, so a plain --target <ip> still finds it.
    """
    keys = []
    cfg_path = os.path.expanduser("~/.ssh/config")

    if os.path.exists(cfg_path):
        cfg = paramiko.SSHConfig()
        with open(cfg_path) as handle:
            cfg.parse(handle)
        entry = cfg.lookup(host)

        host = entry.get("hostname", host)
        # An explicit --target user@host or --user beats the config file.
        if "@" not in args.target and args.user == "root":
            user = entry.get("user", user)
        keys.extend(os.path.expanduser(k) for k in entry.get("identityfile", []))

    if getattr(args, "key", None):
        keys.insert(0, os.path.expanduser(args.key))

    fleet = os.path.expanduser(_FLEET_KEY)
    if os.path.exists(fleet) and fleet not in keys:
        keys.append(fleet)

    return host, user, [k for k in keys if os.path.exists(k)]


def _local_binary(triple):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))   # libs/JUCE
    return os.path.join(root, "build", "frame_rate_bench", triple, _BINARY_NAME)


def main():
    parser = argparse.ArgumentParser(description="Deploy Frame Rate Bench to a QNX target via SFTP")
    parser.add_argument("--target", required=True,
                        help="Target as [user@]host, e.g. root@192.168.1.50 or just 192.168.1.50")
    parser.add_argument("--user", default="root",
                        help="SSH user when not given in --target (default: root)")
    parser.add_argument("--deploy-path", default="~/framerate-bench",
                        help="Destination directory on target (default: ~/framerate-bench)")
    parser.add_argument("--target-triple", default=_DEFAULT_TRIPLE,
                        help=f"Build triple to locate the binary (default: {_DEFAULT_TRIPLE})")
    parser.add_argument("--extra", action="append", default=[],
                        help="Additional local file to ship alongside the binary (repeatable)")
    parser.add_argument("--run", action="store_true",
                        help="Launch the binary over SSH after deploying (backgrounded)")
    parser.add_argument("--env", action="append", default=[],
                        help="KEY=VAL env var for the --run launch (repeatable), "
                             "e.g. --env JUCE_QNX_FAST_PRESENT=1 --env BENCH_SECONDS=20")
    parser.add_argument("--no-deploy", action="store_true",
                        help="Skip pushing the binary (use with --pull-log to just fetch results)")
    parser.add_argument("--pull-log", action="store_true",
                        help="Download <deploy-path>/FrameRateBench.log back to build/frame_rate_bench/results/")
    parser.add_argument("--password-env", default="FRB_SSH_PASSWORD",
                        help="Env var holding the SSH password, for unattended runs "
                             "(default: FRB_SSH_PASSWORD). Ignored if unset/empty.")
    parser.add_argument("--no-prompt", action="store_true",
                        help="Never prompt for a password; rely on the SSH key/agent")
    parser.add_argument("--key", "-i",
                        help=f"SSH private key to use (default: ~/.ssh/config IdentityFile, "
                             f"else {_FLEET_KEY} if present)")
    args = parser.parse_args()

    deploy = not args.no_deploy

    binary = _local_binary(args.target_triple)
    if deploy and not os.path.exists(binary):
        raise SystemExit(f"Binary not found: {binary}\n"
                         f"Build it first:  extras\\FrameRateBench\\build_frame_rate_bench_qnx.bat")

    user, _, host = args.target.rpartition('@')
    user = user or args.user

    # paramiko ignores ~/.ssh/config, so resolve Host aliases (pi4, pi5, ...)
    # and their IdentityFile here. Without this a --target of "pi4" would fail
    # DNS, and a non-default key name would never be tried.
    host, user, key_files = _resolve_ssh_config(host, user, args)

    password = os.environ.get(args.password_env, "")

    # A usable key means no reason to ask for a password at all.
    if not password and not args.no_prompt and not key_files:
        password = getpass.getpass(f"Password for {user}@{host} (blank = use SSH key/agent): ")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, username=user,
                   password=password or None,
                   key_filename=key_files or None,
                   look_for_keys=not password,
                   allow_agent=not password)

    # Resolve ~ to the target's $HOME.
    deploy_dir = args.deploy_path
    if '~' in deploy_dir:
        _, stdout, _ = client.exec_command('echo $HOME')
        home = stdout.read().decode().strip()
        deploy_dir = deploy_dir.replace('~', home)
    deploy_dir = deploy_dir.rstrip('/')

    if deploy:
        # Best-effort stop of a running instance (QNX uses `slay`); non-fatal.
        # exec_command is asynchronous, so wait for each to finish — otherwise
        # the upload can start before mkdir has created the directory, or before
        # the old binary has exited (SFTP cannot overwrite a running binary).
        # slay is QNX; pkill is Linux. Try both so one script serves both targets.
        stop_cmd = f'slay {_BINARY_NAME} 2>/dev/null || pkill -f {_BINARY_NAME} 2>/dev/null || true'

        for cmd in (stop_cmd, f'mkdir -p "{deploy_dir}"'):
            _, out, _ = client.exec_command(cmd)
            out.channel.recv_exit_status()

        sftp = client.open_sftp()

        remote_bin = f"{deploy_dir}/{_BINARY_NAME}"
        print(f"  -> {_BINARY_NAME}")
        sftp.put(binary, remote_bin)
        sftp.chmod(remote_bin, 0o755)

        # The matrix runner is how the numbers actually get produced on target,
        # so it ships automatically rather than being a thing to remember.
        for script in (_MATRIX_SCRIPT, _SWEEP_SCRIPT):
            local_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), script)
            if os.path.exists(local_script):
                print(f"  -> {script}")
                remote_script = f"{deploy_dir}/{script}"
                sftp.put(local_script, remote_script)
                sftp.chmod(remote_script, 0o755)

        for extra in args.extra:
            if not os.path.exists(extra):
                raise SystemExit(f"--extra not found: {extra}")
            fname = os.path.basename(extra)
            print(f"  -> {fname}")
            sftp.put(extra, f"{deploy_dir}/{fname}")

        sftp.close()

    env_prefix = " ".join(args.env)
    run_cmd = f'cd "{deploy_dir}" && {env_prefix} ./{_BINARY_NAME}'.replace("  ", " ").strip()

    if args.run:
        print(f"\nLaunching on target: {run_cmd}")
        # Backgrounded so the SSH call returns; the bench attaches to QNX Screen.
        client.exec_command(f'{run_cmd} >/dev/null 2>&1 &')

    if args.pull_log:
        import datetime
        ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        # Land it in the current working directory so it's easy to find.
        local_log = os.path.abspath(f"FrameRateBench-{host.replace(':', '_')}-{ts}.log")
        remote_log = f"{deploy_dir}/FrameRateBench.log"
        sftp = client.open_sftp()
        try:
            sftp.get(remote_log, local_log)
            print(f"\nPulled log -> {local_log}")
        except FileNotFoundError:
            print(f"\nNo log at {remote_log} (has the bench run? check --deploy-path matches where it ran)")
        finally:
            sftp.close()

    client.close()

    print(f"\n{'Deployed to' if deploy else 'Connected to'} {host}:{deploy_dir}")
    print("\nTo run on the target (from a terminal attached to the display):")
    print(f"  {run_cmd if env_prefix else f'cd {deploy_dir} && ./{_BINARY_NAME}'}")
    print(f"\nOr run the whole comparison matrix there (recommended — a GUI app")
    print(f"generally will not attach to the display over SSH):")
    print(f"  cd {deploy_dir} && ./{_MATRIX_SCRIPT}")
    print(f"then bring the results back and tabulate them:")
    print(f"  python deploy.py --target {user}@{host} --no-deploy --pull-log")
    print(f"  python summarize_results.py FrameRateBench-{host}-<timestamp>.log")
    print("\nUseful env vars:")
    print("  BENCH_RENDERER=opengl     use the GL/EGL present path (default: software)")
    print("  JUCE_QNX_FAST_PRESENT=1   dirty-region software present path (0 = baseline)")
    print("  JUCE_QNX_LOG_FPS=1        emit QNX_PRESENT_FPS from the present/swap path (both renderers)")
    print("  JUCE_QNX_LOG_VERBOSE=1    restore full windowing/GL diagnostics (costs frame rate)")
    print("  BENCH_MODE=partial        small dirty rect (A/B probe for the fast software path)")
    print("  BENCH_SECONDS=20 BENCH_COMPLEXITY=4000 BENCH_TAG=SW-QNX")


if __name__ == "__main__":
    main()
