"""Run deployed binaries with SDK paths removed; not a substitute for clean-OS acceptance."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    package = args.package_dir.resolve()
    windows = sys.platform == "win32"
    binaries = package if windows else package / "CampusPrint.app" / "Contents" / "MacOS"
    suffix = ".exe" if windows else ""
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
    print("Deployed GUI startup and supervisor/worker printing passed with SDK paths removed.")


if __name__ == "__main__":
    main()
