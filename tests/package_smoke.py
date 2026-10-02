"""Run deployed binaries with SDK paths removed; not a substitute for clean-OS acceptance."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    package = args.package_dir.resolve()
    windows = sys.platform == "win32"
    binaries = package if windows else package / "CampusPrint.app" / "Contents" / "MacOS"
    suffix = ".exe" if windows else ""
    if windows:
        from windows_desktop import check_gui_binary, check_backend_consoles
        check_gui_binary(binaries / "campus-print.exe")
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("QT_", "QML", "DYLD_")) and key not in ("QTDIR", "QT_PLUGIN_PATH")}
    env["PATH"] = str(Path(env.get("SystemRoot", "C:/Windows")) / "System32") if windows else "/usr/bin:/bin"
    # Validate the platform plugin shipped to end users, not an SDK-only test plugin.
    env["QT_QPA_PLATFORM"] = "windows" if windows else "cocoa"
    with tempfile.TemporaryDirectory(prefix="package-test-") as temp:
        root = Path(temp) / "中文 空格路径"
        root.mkdir()
        result = subprocess.run([str(binaries / ("campus-print" + suffix)), "--smoke-test", "--data-dir", str(root)],
                                env=env, cwd=root, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Deployed GUI failed ({result.returncode}): {result.stderr!r}")
        # A separate directory exercises the student's own detached backend launch.
        student_root = root / "student"
        student = subprocess.run([str(binaries / ("campus-print" + suffix)), "--role", "student",
                                  "--smoke-test", "--data-dir", str(student_root)],
                                 env=env, cwd=root, capture_output=True, timeout=20)
        if student.returncode:
            raise RuntimeError(f"Deployed student frontend failed ({student.returncode}): {student.stderr!r}")
        pdfium_name = "pdfium.dll" if windows else "libpdfium.dylib"
        if not (binaries / pdfium_name).is_file() or not (package / "licenses" / "pdfium").is_dir():
            raise RuntimeError("Deployed package is missing its PDF renderer or licenses")
        coordinator = binaries / ("print-coordinator" + suffix)
        def request(command):
            response = subprocess.run([str(coordinator), "--state-dir", str(root),
                                       "--request", json.dumps(command)], env=env, cwd=root,
                                      capture_output=True, encoding="utf-8", timeout=12)
            if response.returncode:
                raise RuntimeError(f"Deployed coordinator request failed: {response.stderr} {response.stdout}")
            return json.loads(response.stdout)
        # Validate and stop the student's otherwise persistent backend as well.
        student_coordinator = subprocess.run([str(coordinator), "--state-dir", str(student_root),
                                              "--request", '{"type":"snapshot"}'],
                                             env=env, capture_output=True, timeout=12)
        try:
            if student_coordinator.returncode:
                raise RuntimeError("Student frontend did not start its own coordinator")
            if windows:
                observed = check_backend_consoles(json.loads(student_coordinator.stdout)["coordinator_pid"])
                if "print-coordinator.exe" not in observed:
                    raise RuntimeError("Student coordinator console check did not observe a live process")
        finally:
            subprocess.run([str(coordinator), "--state-dir", str(student_root), "--request", '{"type":"shutdown"}'],
                           env=env, capture_output=True, check=True, timeout=12)
        try:
            request(dict(type="set_paused", paused=True))
            request(dict(type="submit_batch", jobs=[
                dict(job_id="persist-a", title="发布包持久队列", pages=20, copies=1),
                dict(job_id="persist-b", title="关闭后恢复", pages=2, copies=1)]))
            # The first GUI already exited. A second GUI must attach to the same queue.
            again = subprocess.run([str(binaries / ("campus-print" + suffix)), "--smoke-test",
                                    "--data-dir", str(root)], env=env, cwd=root, capture_output=True, timeout=20)
            if again.returncode:
                raise RuntimeError(f"Deployed GUI reopen failed: {again.stderr!r}")
            snapshot = request(dict(type="snapshot"))
            if len(snapshot["jobs"]) != 2 or not snapshot["paused"]:
                raise RuntimeError("Deployed GUI failed to preserve the shared queue/pause state")
            request(dict(type="set_paused", paused=False))
            deadline = time.monotonic() + 15
            observed = set()
            while time.monotonic() < deadline:
                snapshot = request(dict(type="snapshot"))
                if windows:
                    observed |= check_backend_consoles(snapshot["coordinator_pid"])
                if snapshot["metrics"]["completed"] == 2:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Deployed coordinator did not finish both queued jobs")
            if windows and observed != {"print-coordinator.exe", "print-supervisor.exe", "print-worker.exe"}:
                raise RuntimeError(f"Console audit did not cover the complete backend: {observed}")
            for item in snapshot["jobs"]:
                output = root / "output" / (item["job_id"] + ".txt")
                if item["attempts"] != 1 or output.read_text(encoding="utf-8").count("sheet=") != item["pages"]:
                    raise RuntimeError("Deployed persistent job duplicated or produced incorrect output")
        finally:
            request(dict(type="shutdown"))
            deadline = time.monotonic() + 5
            while (root / ".coordinator.lock").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            if (root / ".coordinator.lock").exists():
                raise RuntimeError("Deployed coordinator did not shut down")
        job = root / "任务.json"
        job.write_text(json.dumps(dict(schema_version=1, job_id="package-job", title="发布包测试", pages=2, copies=1),
                                  ensure_ascii=False), encoding="utf-8")
        result = subprocess.run([str(binaries / ("print-supervisor" + suffix)), "--job", str(job),
                                 "--output-dir", str(root), "--page-ms", "10"],
                                env=env, cwd=root, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Deployed supervisor failed ({result.returncode}): {result.stderr!r}")
        events = [json.loads(line) for line in result.stdout.decode("utf-8").splitlines()]
        if not events or events[-1]["type"] != "supervisor_completed":
            raise RuntimeError("Deployed supervisor did not report completion")
        content = (root / "package-job.txt").read_text(encoding="utf-8")
        if content.count("sheet=") != 2:
            raise RuntimeError("Deployed worker output does not match the requested job")
    print("Deployed GUI reopen, persistent coordinator queue and supervisor/worker printing passed with SDK paths removed.")
    if windows:
        print("Windows GUI subsystem, embedded app icon and console-free coordinator/supervisor/worker verified.")


if __name__ == "__main__":
    main()
