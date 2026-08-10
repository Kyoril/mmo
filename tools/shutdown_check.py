#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Verifies that each server tier exits cleanly when asked to stop.

Why this exists as its own tool rather than an E2E scenario: the E2E harness launches its
servers hidden with redirected stdio (see Start-E2eServer in tools/e2e/e2e_common.psm1), so
they have neither a window to close nor a shared console to signal -- it can only ever
force-kill them, which tells you nothing about graceful shutdown.

Each server is launched in its OWN process group (CREATE_NEW_PROCESS_GROUP) so the
CTRL_BREAK_EVENT reaches only that server. Sending to group 0 would hit every process
sharing the console, including the shell that started this script. CTRL_BREAK maps to
SIGBREAK, which InstallShutdownHandler registers on Windows alongside SIGINT and SIGTERM;
a process created with CREATE_NEW_PROCESS_GROUP has Ctrl+C disabled by default, so
Ctrl+Break is the correct event here. In Docker the equivalent signal is SIGTERM.

The servers are exercised one at a time, NOT as a connected stack. That is deliberate: with
no login server up the realm server is retrying its upstream login, and with no realm server
up the world server is sitting in its five-second reconnect loop. Those are the states where
shutdown is most likely to hang, so they are the ones worth checking.

A tier passes only if all of these hold:
  * it exits within the timeout,
  * with exit code 0 (a process killed by an unhandled Ctrl+Break exits 0xC000013A),
  * its log ends with a "stopped cleanly" line, and
  * any attached client observes a closed connection rather than a silent socket.

Usage (from the repo root, after building the servers and running the E2E stack at least
once so that e2e/runtime/config exists):

    python tools/shutdown_check.py

Exits 0 if every tier shuts down cleanly.
"""

import argparse
import glob
import os
import signal
import socket
import subprocess
import sys
import time


def newest_log(log_dir):
    logs = glob.glob(os.path.join(log_dir, "*.log"))
    if not logs:
        return None
    return max(logs, key=os.path.getmtime)


def wait_for_port(port, timeout=30.0):
    """Returns True once something is accepting on the port."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.25)
    return False


def check(name, exe, config, workdir, port, startup_grace, signal_timeout):
    """Starts one server, signals it, and reports whether it stopped cleanly."""
    os.makedirs(workdir, exist_ok=True)
    print(f"--- {name} ---")

    if not os.path.exists(exe):
        print(f"{name}: FAIL - {exe} not found (build it first)")
        return False
    if not os.path.exists(config):
        print(f"{name}: FAIL - config {config} not found")
        return False

    proc = subprocess.Popen(
        [exe, "-c", config],
        cwd=workdir,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    if port is not None:
        if not wait_for_port(port):
            proc.kill()
            print(f"{name}: FAIL - never started listening on {port}")
            return False
        print(f"{name}: listening on {port}")
    else:
        # No listening port of its own (the world node only dials out), so fall back to a
        # fixed grace period for startup.
        time.sleep(startup_grace)
        print(f"{name}: started")

    client = None
    if port is not None:
        client = socket.create_connection(("127.0.0.1", port), timeout=2)
        time.sleep(0.8)
        print(f"{name}: client attached")

    clean = True
    started = time.time()
    proc.send_signal(signal.CTRL_BREAK_EVENT)

    try:
        code = proc.wait(timeout=signal_timeout)
        print(f"{name}: exited in {time.time() - started:.1f}s with code {code}")
        if code != 0:
            clean = False
    except subprocess.TimeoutExpired:
        proc.kill()
        print(f"{name}: FAIL - did not exit within {signal_timeout}s of the signal")
        clean = False

    if client is not None:
        # After a clean shutdown the peer must see the connection closed, not a socket that
        # merely stopped answering -- that distinction is the whole point of closing rather
        # than releasing the connection.
        client.settimeout(3)
        try:
            data = client.recv(64)
            if data == b"":
                print(f"{name}: client saw EOF (closed)")
            else:
                print(f"{name}: FAIL - client got unexpected data instead of a close")
                clean = False
        except socket.timeout:
            print(f"{name}: FAIL - client never saw a close")
            clean = False
        except OSError as err:
            print(f"{name}: client socket errored ({err.__class__.__name__}) - counts as closed")
        client.close()

    log = newest_log(os.path.join(workdir, "logs"))
    if log:
        with open(log, "r", encoding="utf-8", errors="replace") as handle:
            tail = [line.rstrip() for line in handle.readlines()[-6:]]
        print(f"{name}: log tail:")
        for line in tail:
            print(f"    {line}")
        if not any("stopped cleanly" in line for line in tail):
            print(f"{name}: FAIL - log does not end with a clean-stop line")
            clean = False
    else:
        print(f"{name}: FAIL - no log file found under {workdir}")
        clean = False

    print(f"{name}: {'PASS' if clean else 'FAIL'}\n")
    return clean


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", default=os.path.join("bin", "Debug"),
                        help="directory holding the built servers (default: bin/Debug)")
    parser.add_argument("--config-dir", default=os.path.join("e2e", "runtime", "config"),
                        help="directory holding the server .cfg files "
                             "(default: e2e/runtime/config, created by tools/e2e/e2e_up.ps1)")
    parser.add_argument("--work-dir", default=os.path.join("e2e", "runtime", "shutdown_check"),
                        help="scratch directory for the servers' working dirs and logs")
    parser.add_argument("--signal-timeout", type=float, default=25.0,
                        help="seconds to allow between the signal and the process exiting")
    args = parser.parse_args()

    if sys.platform != "win32":
        print("This tool drives Windows console control events. On Linux, send SIGTERM "
              "(for example `docker stop`) and check for the same 'stopped cleanly' lines.")
        return 2

    tiers = [
        ("login_server", "login_server.cfg", 13724, 8.0),
        ("realm_server", "realm_server.cfg", 18129, 12.0),
        ("world_server", "world_server.cfg", None, 10.0),
    ]

    # Absolute, because each server is launched with its working directory set to its own
    # scratch dir -- a relative exe or config path would then resolve against that instead of
    # the repo root.
    binaries = os.path.abspath(args.bin)
    configs = os.path.abspath(args.config_dir)
    workroot = os.path.abspath(args.work_dir)

    results = []
    for name, config_name, port, grace in tiers:
        results.append(check(
            name,
            os.path.join(binaries, name + ".exe"),
            os.path.join(configs, config_name),
            os.path.join(workroot, name),
            port,
            grace,
            args.signal_timeout,
        ))

    ok = all(results)
    print("ALL PASS" if ok else "SOME FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
