"""Real supervisor control, reconnection, cancellation and lease cleanup tests."""
import argparse
import json
import os
from pathlib import Path
import queue
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import uuid

parser = argparse.ArgumentParser()
parser.add_argument("--supervisor", type=Path, required=True)
ARGS = parser.parse_args()


class ControlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cps-control-")
        self.root = Path(self.temp.name) / "中文 控制"
        self.root.mkdir()
        self.job = self.root / "job.json"
        self.job.write_text(json.dumps(dict(schema_version=1, job_id="controlled", title="控制 测试", pages=60, copies=1)), encoding="utf-8")
        self.token = uuid.uuid4().hex
        self.process = None
        self.client = None
        self.worker_pid = None
        self.events = queue.Queue()
        self.seen = []

    def start(self, demo=False, lease=8000, completion_delay=0):
        command = [str(ARGS.supervisor), "--job", str(self.job), "--output-dir", str(self.root),
                   "--page-ms", "20", "--control-token", self.token, "--claim-attempt", "2", "--lease-ms", str(lease),
                   "--demo-completion-delay-ms", str(completion_delay)]
        if demo:
            command.append("--demo-mode")
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        def reader():
            for line in self.process.stdout:
                self.events.put(json.loads(line))
        self.thread = threading.Thread(target=reader, daemon=True)
        self.thread.start()
        self.port = self.wait_event("control_ready")["port"]
        self.connect_client()
        self.worker_pid = self.wait_event("worker_started")["pid"]

    def connect_client(self):
        self.client = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        self.reader = self.client.makefile("rb")
        self.send("subscribe")
        result = self.response("control_state")
        self.assertEqual(result["claim_attempt"], 2)

    def send(self, command, **overrides):
        value = dict(schema_version=1, job_id="controlled", claim_attempt=2, token=self.token,
                     command=command, request_id=uuid.uuid4().hex)
        value.update(overrides)
        self.client.sendall(json.dumps(value).encode() + b"\n")
        return value["request_id"]

    def response(self, kind):
        for _ in range(50):
            line = self.reader.readline()
            self.assertTrue(line, "control connection closed without response")
            value = json.loads(line)
            if value["type"] == kind:
                return value
        self.fail("response not received")

    def wait_event(self, kind, timeout=10):
        # Consume chronologically; remember worker identities for cleanup.
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                value = self.events.get(timeout=0.2)
            except queue.Empty:
                continue
            self.seen.append(value)
            if value["type"] == "worker_started":
                self.worker_pid = value["pid"]
            if value["type"] == kind:
                return value
        self.fail(f"No {kind}; seen={self.seen}")

    def disconnect(self):
        if self.client:
            self.reader.close()
            self.client.close()
            self.client = None

    def tearDown(self):
        self.disconnect()
        if self.process:
            if self.process.poll() is None:
                if self.worker_pid:
                    try:
                        os.kill(int(self.worker_pid), signal.SIGTERM)
                    except OSError:
                        pass
                self.process.kill()
            self.process.wait(timeout=5)
            self.thread.join(timeout=2)
            self.process.stdout.close()
            self.process.stderr.close()
        self.temp.cleanup()

    def test_cancel_stops_worker_and_preserves_previous_output(self):
        previous = self.root / "controlled.txt"
        previous.write_text("previous completed output", encoding="utf-8")
        self.start()
        self.wait_event("progress")
        self.send("cancel")
        self.assertTrue(self.response("control_ack")["accepted"])
        self.assertEqual(self.wait_event("cancelled")["claim_attempt"], 2)
        self.assertEqual(self.process.wait(timeout=5), 130)
        self.assertEqual(previous.read_text(encoding="utf-8"), "previous completed output")
        # A stopped worker releases its output lock: another supervisor can run this ID.
        result = subprocess.run([str(ARGS.supervisor), "--job", str(self.job), "--output-dir", str(self.root),
                                 "--page-ms", "10"], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_disconnect_reconnect_and_real_worker_crash(self):
        self.start(demo=True)
        self.wait_event("progress")
        self.disconnect()
        self.connect_client()
        self.send("crash_worker")
        self.assertTrue(self.response("control_ack")["accepted"])
        self.wait_event("worker_restarting")
        next_worker = self.wait_event("worker_started")
        self.assertEqual(next_worker["attempt"], 2)
        self.wait_event("supervisor_completed")
        self.assertEqual(self.process.wait(timeout=5), 0)
        self.assertEqual((self.root / "controlled.txt").read_text(encoding="utf-8").count("sheet="), 60)

    def test_fault_disabled_outside_demo_and_stale_attempt_rejected(self):
        self.start()
        self.send("crash_worker")
        self.assertFalse(self.response("control_ack")["accepted"])
        self.send("cancel", claim_attempt=1)
        self.assertFalse(self.response("control_ack")["accepted"])
        self.send("cancel")
        self.assertTrue(self.response("control_ack")["accepted"])
        self.wait_event("cancelled")
        self.assertEqual(self.process.wait(timeout=5), 130)

    def test_missing_heartbeat_cancels_instead_of_leaving_worker(self):
        self.start(lease=500)
        self.wait_event("progress")
        self.disconnect()
        self.wait_event("cancelled")
        self.assertEqual(self.process.wait(timeout=5), 130)
        self.assertFalse((self.root / "controlled.txt").exists())

    def test_control_token_not_written_to_logs(self):
        self.start()
        self.send("cancel", token="incorrect")
        self.assertFalse(self.response("control_ack")["accepted"])
        self.send("cancel")
        self.response("control_ack")
        self.wait_event("cancelled")
        self.process.wait(timeout=5)
        self.assertNotIn(self.token, (self.root / "controlled.events.jsonl").read_text(encoding="utf-8"))

    def test_committed_output_wins_cancel_race(self):
        self.start(demo=True, completion_delay=1500)
        self.wait_event("completed")
        self.send("cancel")
        self.response("control_ack")
        self.wait_event("supervisor_completed")
        self.assertEqual(self.process.wait(timeout=5), 0)
        self.assertEqual((self.root / "controlled.txt").read_text(encoding="utf-8").count("sheet="), 60)

    def test_cancel_during_restart_backoff_does_not_restart(self):
        self.start(demo=True)
        self.send("crash_worker")
        self.response("control_ack")
        self.wait_event("worker_restarting")
        self.send("cancel")
        self.response("control_ack")
        self.wait_event("cancelled")
        self.assertEqual(self.process.wait(timeout=5), 130)
        self.assertEqual(sum(e["type"] == "worker_started" for e in self.seen), 1)

    def test_control_commands_can_be_fragmented_and_coalesced(self):
        self.start()
        command = dict(schema_version=1, job_id="controlled", claim_attempt=2, token=self.token,
                       command="ping", request_id="split-command")
        first = json.dumps(command).encode() + b"\n"
        command["request_id"] = "coalesced-command"
        second = json.dumps(command).encode() + b"\n"
        self.client.sendall(first[:7])
        self.client.sendall(first[7:] + second)
        self.assertEqual(self.response("control_ack")["request_id"], "split-command")
        self.assertEqual(self.response("control_ack")["request_id"], "coalesced-command")
        self.send("cancel")
        self.response("control_ack")
        self.wait_event("cancelled")
        self.assertEqual(self.process.wait(timeout=5), 130)


unittest.main(argv=["control_integration"], verbosity=2)
