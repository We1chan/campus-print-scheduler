"""Real-process tests. No third-party Python packages; Windows/macOS/Linux."""
import argparse
import json
import os
from pathlib import Path
import queue
import signal
import subprocess
import tempfile
import threading
import time
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--worker", type=Path, required=True)
parser.add_argument("--supervisor", type=Path, required=True)
parser.add_argument("--gui", type=Path, required=True)
ARGS = parser.parse_args()


class Processes(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="campus-print-")
        self.root = Path(self.temp.name) / "中文 路径 with spaces"
        self.root.mkdir()
        self.job = self.root / "任务.json"
        self.output = self.root / "打印 输出"
        self.write_job()

    def tearDown(self):
        self.temp.cleanup()

    def write_job(self, **overrides):
        value = dict(schema_version=1, job_id="test-job", title="操作系统 测试", pages=4, copies=2)
        value.update(overrides)
        self.job.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    def command(self, executable, *extra):
        return [str(executable), "--job", str(self.job), "--output-dir", str(self.output),
                "--page-ms", "20", *extra]

    def run_command(self, command, env=None):
        return subprocess.run(command, capture_output=True, encoding="utf-8", timeout=20, env=env)

    def test_worker_unicode_and_atomic_output(self):
        result = self.run_command(self.command(ARGS.worker))
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[-1]["type"], "completed")
        content = (self.output / "test-job.txt").read_text(encoding="utf-8")
        self.assertIn("操作系统 测试", content)
        self.assertEqual(content.count("sheet="), 8)

    def test_reject_path_traversal_and_fractional_pages(self):
        for overrides in ({"job_id": "../escape"}, {"pages": 1.5}, {"pages": 0}, {"schema_version": 2}):
            self.write_job(**overrides)
            result = self.run_command(self.command(ARGS.worker))
            self.assertEqual(result.returncode, 64, result.stderr)
            self.assertFalse((self.output / "test-job.txt").exists())

    def test_supervisor_silence_watchdog_times_out_and_gives_up(self):
        # Demo mode may shorten the watchdog below the production 20 s floor.
        # A worker printing one sheet every 10 s stays silent for the whole
        # 1.5 s window, so every attempt must end in worker_timeout.
        command = [str(ARGS.supervisor), "--job", str(self.job), "--output-dir", str(self.output),
                   "--page-ms", "10000", "--max-restarts", "1", "--timeout-ms", "1500",
                   "--demo-mode"]
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=25)
        self.assertEqual(result.returncode, 70, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(event["type"] == "worker_started" for event in events), 2)
        self.assertEqual(sum(event["type"] == "worker_timeout" for event in events), 2)
        self.assertEqual(sum(event["type"] == "worker_restarting" for event in events), 1)
        self.assertEqual(events[-1]["type"], "failed")
        self.assertEqual(events[-1]["restarts"], 1)
        self.assertFalse((self.output / "test-job.txt").exists())

    def test_supervisor_normal_completion(self):
        result = subprocess.run(self.command(ARGS.supervisor), capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b"\r", result.stdout, "IPC must preserve LF instead of CRT CRCRLF translation")
        events = [json.loads(line) for line in result.stdout.decode("utf-8").splitlines()]
        self.assertEqual(events[-1]["type"], "supervisor_completed")
        self.assertEqual(events[-1]["restarts"], 0)

    def test_worker_crash_restarts_without_partial_output(self):
        self.write_job(pages=20, copies=1)
        process = subprocess.Popen(self.command(ARGS.supervisor), stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8")
        events = queue.Queue()
        def reader():
            for line in process.stdout:
                events.put(json.loads(line))
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        seen = []
        killed = False
        current_pid = None
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                try:
                    event = events.get(timeout=0.2)
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                seen.append(event)
                if event["type"] == "worker_started":
                    current_pid = int(event["pid"])
                if event["type"] == "progress" and not killed:
                    self.assertFalse((self.output / "test-job.txt").exists())
                    os.kill(current_pid, signal.SIGTERM)
                    killed = True
                if event["type"] == "supervisor_completed":
                    break
            self.assertEqual(process.wait(timeout=5), 0)
            self.assertTrue(killed)
            self.assertTrue(any(event["type"] == "worker_restarting" for event in seen))
            content = (self.output / "test-job.txt").read_text(encoding="utf-8")
            self.assertEqual(content.count("sheet="), 20)
        finally:
            if process.poll() is None:
                if current_pid:
                    try:
                        os.kill(current_pid, signal.SIGTERM)
                    except OSError:
                        pass
                process.kill()
            process.wait(timeout=5)
            thread.join(timeout=2)
            process.stdout.close()
            process.stderr.close()

    def test_same_job_lock(self):
        self.write_job(pages=100, copies=1)
        first = subprocess.Popen(self.command(ARGS.worker), stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, encoding="utf-8")
        try:
            first.stdout.readline()  # Progress means the output lock is held.
            second = self.run_command(self.command(ARGS.worker))
            self.assertEqual(second.returncode, 75)
        finally:
            first.kill()
            first.communicate(timeout=5)

    def test_restart_limit(self):
        self.output.mkdir()
        (self.output / "test-job.txt").mkdir()  # A directory cannot be replaced by worker output.
        result = self.run_command(self.command(ARGS.supervisor, "--max-restarts", "2"))
        self.assertEqual(result.returncode, 70, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(sum(event["type"] == "worker_restarting" for event in events), 2)
        self.assertEqual(events[-1]["type"], "failed")

    def test_supervisor_cancel_marker_stops_worker_without_output(self):
        self.write_job(pages=100, copies=1)
        cancel = self.root / "cancel.marker"
        command = self.command(ARGS.supervisor, "--cancel-file", str(cancel))
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding="utf-8")
        received = queue.Queue()
        def reader():
            for line in process.stdout:
                received.put(json.loads(line))
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        events = []
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                try:
                    event = received.get(timeout=0.2)
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                events.append(event)
                if event["type"] == "progress":
                    cancel.write_bytes(b"")
                if event["type"] == "cancelled":
                    break
            # Exit 130 is the documented cancellation code (docs/interfaces.md).
            self.assertEqual(process.wait(timeout=5), 130)
            self.assertTrue(any(event["type"] == "cancelling" for event in events))
            self.assertEqual(events[-1]["type"], "cancelled")
            self.assertFalse((self.output / "test-job.txt").exists())
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            thread.join(timeout=2)
            process.stdout.close()
            process.stderr.close()

    @unittest.skipUnless(os.name == "nt", "Windows-only explicit unsupported-mode check")
    def test_windows_daemon_rejected(self):
        result = self.run_command(self.command(ARGS.supervisor, "--daemon"))
        self.assertEqual(result.returncode, 69, result.stderr)

    def test_gui_offscreen_startup(self):
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        coordinator = ARGS.gui.with_name("print-coordinator" + (".exe" if os.name == "nt" else ""))
        process = subprocess.Popen([str(ARGS.gui), "--smoke-test", "--data-dir", str(self.root / "gui-smoke")],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            # The GUI must release its capture pipes while its coordinator is still alive.
            # Popen lets finally stop that coordinator even if pipe EOF never arrives;
            # subprocess.run's timeout cleanup can itself hang on Windows in that case.
            _, stderr = process.communicate(timeout=15)
            self.assertEqual(process.returncode, 0, stderr.decode("utf-8", errors="replace"))
        finally:
            if process.poll() is None:
                process.kill()
            stopped = self.run_command([str(coordinator), "--state-dir", str(self.root / "gui-smoke"),
                                        "--request", json.dumps({"type": "shutdown"})])
            process.communicate(timeout=5)
            self.assertEqual(stopped.returncode, 0, stopped.stderr)
            deadline = time.monotonic() + 5
            while (self.root / "gui-smoke" / ".coordinator.lock").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse((self.root / "gui-smoke" / ".coordinator.lock").exists())

    def test_student_client_offscreen_startup(self):
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        data = self.root / "student-smoke"
        coordinator = ARGS.gui.with_name("print-coordinator" + (".exe" if os.name == "nt" else ""))
        for arguments in (["--role", "teacher"], ["--role", "student", "--legacy-mode"]):
            rejected = self.run_command([str(ARGS.gui), *arguments, "--smoke-test", "--data-dir", str(data)], env=env)
            self.assertEqual(rejected.returncode, 64, rejected.stderr)
        self.assertFalse(data.exists(), "rejected options must not create a data directory")
        # The student client is a second front end for the same coordinator and may start it.
        process = subprocess.Popen([str(ARGS.gui), "--role", "student", "--student", "2024000001", "--smoke-test",
                                    "--data-dir", str(data)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            _, stderr = process.communicate(timeout=15)
            self.assertEqual(process.returncode, 0, stderr.decode("utf-8", errors="replace"))
            snapshot = self.run_command([str(coordinator), "--state-dir", str(data), "--request", json.dumps({"type": "snapshot"})])
            self.assertEqual(snapshot.returncode, 0, snapshot.stderr)
            self.assertTrue(json.loads(snapshot.stdout)["shop_enabled"])
        finally:
            if process.poll() is None:
                process.kill()
            stopped = self.run_command([str(coordinator), "--state-dir", str(data), "--request", json.dumps({"type": "shutdown"})])
            process.communicate(timeout=5)
            self.assertEqual(stopped.returncode, 0, stopped.stderr)
            deadline = time.monotonic() + 5
            while (data / ".coordinator.lock").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertFalse((data / ".coordinator.lock").exists())

    @unittest.skipIf(os.name == "nt", "POSIX daemon mode; Windows uses foreground supervision")
    def test_posix_daemon(self):
        result = self.run_command(self.command(ARGS.supervisor, "--daemon"))
        self.assertEqual(result.returncode, 0, result.stderr)
        log = self.output / "test-job.events.jsonl"
        completed = False
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if log.exists() and '"supervisor_completed"' in log.read_text(encoding="utf-8"):
                completed = True
                break
            time.sleep(0.05)
        self.assertTrue(completed, "daemon did not finish its finite demo job")


unittest.main(argv=["process_integration"], verbosity=2)
