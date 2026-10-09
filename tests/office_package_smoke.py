"""Verify bundled Office import AND actual PDF rendering without SDK paths."""
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
    executable = binaries / ("campus-print.exe" if windows else "campus-print")
    env = {key: value for key, value in os.environ.items() if not key.startswith(
        ("QT_", "QML", "DYLD_", "CPS_OFFICE_")) and key != "QTDIR"}
    env["PATH"] = str(Path(env.get("SystemRoot", "C:/Windows")) / "System32") if windows else "/usr/bin:/bin"
    env["QT_QPA_PLATFORM"] = "windows" if windows else "cocoa"
    engine = (binaries / "office/program/soffice.com") if windows else package / "office/LibreOffice.app/Contents/MacOS/soffice"
    if not engine.is_file():
        raise RuntimeError("Bundled Office preview engine missing")
    # Exercise production discovery without an override. The reported engine
    # must be bundled, so a system installation cannot hide deployment defects.
    with tempfile.TemporaryDirectory(prefix="paperroom-package-office-") as temporary:
        for suffix in ("doc", "docx", "ppt", "pptx", "xls", "xlsx"):
            fixture = package / "examples" / "office" / ("中文 空格." + suffix)
            report = Path(temporary) / (suffix + ".json")
            subprocess.run([str(executable), "--check-preview", str(fixture), "--preview-report", str(report)],
                           env=env, cwd=temporary, check=True, timeout=110)
            data = json.loads(report.read_text(encoding="utf-8"))
            if not data.get("converter") or Path(data["converter"]).resolve() != engine.resolve():
                raise RuntimeError("Preview did not discover the bundled Office engine: " + str(data))
            if not data["ok"] or data["pages"] != 2 or not data["first_rendered"] or not data["last_rendered"]:
                raise RuntimeError("Deployed preview failed: " + suffix + ": " + str(data))
            if suffix.startswith("ppt") and data["width"] <= data["height"]:
                raise RuntimeError("Deployed PPT preview lost landscape direction")
            print(suffix + ": 2 real pages rendered without SDK paths", flush=True)
    if not windows:
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict",
                        str(package / "office/LibreOffice.app")], check=True)
        print("Office vendor signature remains valid after all six imports", flush=True)


if __name__ == "__main__":
    main()
