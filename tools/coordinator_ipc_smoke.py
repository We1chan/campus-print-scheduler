#!/usr/bin/env python3
"""Coordinator IPC contract smoke test (docs/interfaces.md "Coordinator 本地 IPC v1").

Runs the real print-coordinator and exercises submit/snapshot/cancel/set_policy/
shutdown over the JSONL channel, including a crash-recovery pass in --daemon mode.
POSIX only: on Windows the QLocalServer name maps to a named pipe; use the
coordinator_integration CTest target instead.

Usage:
  coordinator_ipc_smoke.py --coordinator <path> [--state-dir <dir>] [--daemon]
"""
import argparse
import hashlib
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


def socket_candidates(name):
    dirs = [os.environ.get("XDG_RUNTIME_DIR", ""), tempfile.gettempdir(), "/tmp"]
    return [os.path.join(d, name) for d in dirs if d]


class Client:
    def __init__(self, paths, timeout=20.0):
        self.f = None
        deadline = time.time() + timeout
        while time.time() < deadline:
            for path in paths:
                if not os.path.exists(path):
                    continue
                try:
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    s.settimeout(10)
                    s.connect(path)
                    self.f = s.makefile("rwb")
                    self.s = s
                    return
                except (ConnectionRefusedError, OSError):
                    time.sleep(0.3)
            time.sleep(0.2)
        raise SystemExit("FAIL: cannot connect to coordinator socket")

    def req(self, obj):
        obj.setdefault("schema_version", 1)
        obj.setdefault("request_id", "smoke-%d" % (getattr(self, "_n", 0) + 1))
        self._n = getattr(self, "_n", 0) + 1
        self.f.write((json.dumps(obj) + "\n").encode()); self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                raise SystemExit("FAIL: connection closed")
            msg = json.loads(line)
            if msg.get("request_id") == obj["request_id"]:
                return msg

    def jobs(self):
        return {j["job_id"]: j for j in self.req({"type": "snapshot"})["jobs"]}

    def wait_state(self, jid, states, timeout=90):
        deadline = time.time() + timeout
        while time.time() < deadline:
            job = self.jobs().get(jid)
            if job and job["state"] in states:
                return job
            time.sleep(0.4)
        raise SystemExit("FAIL: %s never reached %s" % (jid, states))


def check(cond, label):
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--coordinator", required=True)
    parser.add_argument("--state-dir", default=os.path.join(tempfile.gettempdir(), "cps-ipc-smoke"))
    parser.add_argument("--daemon", action="store_true", help="test --daemon mode plus crash recovery")
    args = parser.parse_args()

    exe = os.path.abspath(args.coordinator)
    state = os.path.abspath(args.state_dir)
    name = "campus-print-" + hashlib.sha256(state.encode()).hexdigest()[:16]
    paths = socket_candidates(name)
    shutil.rmtree(state, ignore_errors=True)
    for p in paths:
        if os.path.exists(p):
            os.remove(p)

    proc = None
    if args.daemon:
        check(subprocess.run([exe, "--daemon", "--state-dir", state]).returncode == 0,
              "daemon parent exits 0")
    else:
        proc = subprocess.Popen([exe, "--state-dir", state])
        time.sleep(0.5)

    c = Client(paths)
    print("connected to", name)

    r = c.req({"type": "submit", "job": {"job_id": "smoke-a", "title": "契约冒烟", "pages": 3, "copies": 1}})
    check(r.get("type") == "accepted", "submit accepted")
    c.wait_state("smoke-a", {"completed"})
    out = os.path.join(state, "output", "smoke-a.txt")
    check(os.path.isfile(out) and "sheet=3" in open(out, encoding="utf-8").read(),
          "atomic output contains all sheets")

    bad = c.req({"type": "submit", "job": {"job_id": "bad id!", "title": "x", "pages": 1, "copies": 1}})
    check(bad.get("type") == "error", "invalid job_id rejected")

    r = c.req({"type": "set_policy", "policy": "sjf"})
    check(r.get("type") == "accepted" and c.req({"type": "snapshot"})["policy"] == "sjf",
          "set_policy round-trips")

    r = c.req({"type": "stats"})
    check(r.get("type") == "stats" and r["statistics"]["completed"] >= 1,
          "stats returns A's counters over IPC")
    bad = c.req({"type": "stats", "window_start_ms": -1})
    check(bad.get("type") == "error", "stats rejects invalid window")

    c.req({"type": "submit", "job": {"job_id": "smoke-cancel", "title": "取消", "pages": 300, "copies": 1}})
    c.wait_state("smoke-cancel", {"running"}, timeout=30)
    r = c.req({"type": "cancel", "job_id": "smoke-cancel"})
    check(r.get("type") == "accepted", "cancel running accepted")
    c.wait_state("smoke-cancel", {"cancelled"}, timeout=30)
    check(not os.path.exists(os.path.join(state, "output", "smoke-cancel.txt")),
          "cancelled job leaves no output")

    if args.daemon:
        c.req({"type": "submit", "job": {"job_id": "smoke-crash", "title": "崩溃恢复", "pages": 200, "copies": 1}})
        c.wait_state("smoke-crash", {"running"}, timeout=30)
        pid = subprocess.run(["pgrep", "-f", "%s --state-dir %s" % (exe, state)],
                             capture_output=True, text=True).stdout.split()
        check(bool(pid), "daemon process found for SIGKILL")
        os.kill(int(pid[0]), signal.SIGKILL)
        time.sleep(1)
        check(subprocess.run([exe, "--daemon", "--state-dir", state]).returncode == 0,
              "daemon restarts after crash")
        c = Client(paths)
        c.wait_state("smoke-crash", {"completed"}, timeout=120)
        check(os.path.isfile(os.path.join(state, "output", "smoke-crash.txt")),
              "output committed after recovery")
        dup = subprocess.run([exe, "--state-dir", state], capture_output=True, text=True, timeout=10)
        check(dup.returncode == 75, "second instance exits 75 (got %s)" % dup.returncode)

    r = c.req({"type": "shutdown"})
    check(r.get("type") == "accepted", "shutdown accepted")
    time.sleep(1.5)
    if proc:
        check(proc.wait(timeout=5) == 0, "foreground coordinator exits 0")
    else:
        pid = subprocess.run(["pgrep", "-f", "%s --state-dir %s" % (exe, state)],
                             capture_output=True, text=True).stdout.split()
        check(not pid, "daemon exits cleanly on shutdown")
    print("ALL CONTRACT CHECKS PASSED")


if __name__ == "__main__":
    main()
