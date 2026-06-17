"""
deploy.py
Deploy the Frame Rate Bench binary from a Windows (or Linux/WSL) host to a QNX
target over SFTP, mirroring the AutoDemos/VREngine deploy pattern.

    python deploy.py --target 192.168.1.50
    python deploy.py --target root@192.168.1.50 --deploy-path /data/home/root/bench
    python deploy.py --target 192.168.1.50 --run --env BENCH_SECONDS=20 --env JUCE_QNX_FAST_PRESENT=1

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
_DEFAULT_TRIPLE = "12.2.0_gcc_ntoaarch64le"   # matches build/ layout from build_frame_rate_bench_qnx.*


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
    args = parser.parse_args()

    deploy = not args.no_deploy

    binary = _local_binary(args.target_triple)
    if deploy and not os.path.exists(binary):
        raise SystemExit(f"Binary not found: {binary}\n"
                         f"Build it first:  extras\\FrameRateBench\\build_frame_rate_bench_qnx.bat")

    user, _, host = args.target.rpartition('@')
    user = user or args.user

    password = getpass.getpass(f"Password for {user}@{host} (blank = use SSH key/agent): ")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, username=user,
                   password=password or None,
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
        client.exec_command(f'slay {_BINARY_NAME} 2>/dev/null')

        client.exec_command(f'mkdir -p "{deploy_dir}"')

        sftp = client.open_sftp()

        remote_bin = f"{deploy_dir}/{_BINARY_NAME}"
        print(f"  -> {_BINARY_NAME}")
        sftp.put(binary, remote_bin)
        sftp.chmod(remote_bin, 0o755)

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
    print("\nUseful env vars:")
    print("  JUCE_QNX_FAST_PRESENT=1   prototype dirty-region present path (0 = baseline)")
    print("  JUCE_QNX_LOG_FPS=1        emit QNX_PRESENT_FPS lines from the present path")
    print("  BENCH_MODE=partial        small dirty rect (A/B probe for the fast path)")
    print("  BENCH_SECONDS=20 BENCH_COMPLEXITY=4000 BENCH_TAG=SW-QNX")


if __name__ == "__main__":
    main()
