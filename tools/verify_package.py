"""Extract, verify and test a desktop artifact on a separate runner without installing Qt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-commit", default=os.environ.get("GITHUB_SHA"),
                        help="Exact producer commit, defaulting to the current CI commit")
    args = parser.parse_args()
    archives = list(args.archive_dir.glob("*.zip")) + list(args.archive_dir.glob("*.tar.gz"))
    if len(archives) != 1 or args.output_dir.exists():
        parser.error("Need exactly one package archive and a new output directory")
    sdk_tools = {name: shutil.which(name) for name in ("qmake", "qmake6", "qtpaths", "qtpaths6")}
    print("Qt SDK tools on this runner's PATH:", sdk_tools, flush=True)
    if any(sdk_tools.values()) and os.environ.get("GITHUB_ACTIONS") == "true":
        raise RuntimeError("Independent runtime runner unexpectedly exposes a Qt SDK")
    args.output_dir.mkdir(parents=True)
    archive = archives[0]
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as file:
            file.extractall(args.output_dir)
    else:
        with tarfile.open(archive) as file:
            file.extractall(args.output_dir, filter="data")
    package = args.output_dir / archive.name.removesuffix(".zip").removesuffix(".tar.gz")
    manifest = json.loads((package / "release-manifest.json").read_text(encoding="utf-8"))
    expected_commit = args.expected_commit
    if expected_commit and (manifest["source_commit"] != expected_commit or manifest["working_tree_dirty"]):
        raise RuntimeError("Package is not from the clean CI commit")
    for entry in manifest["files"]:
        path = (package / entry["path"]).resolve()
        if not path.is_relative_to(package.resolve()):
            raise RuntimeError("Manifest path escapes the package")
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError(f"Package checksum mismatch: {entry['path']}")
    print(f"Verified {len(manifest['files'])} file hashes for {manifest['source_commit']}", flush=True)
    root = Path(__file__).resolve().parents[1]
    subprocess.run([sys.executable, str(root / "tests" / "package_smoke.py"), "--package-dir", str(package)], check=True)
    if manifest.get("office_preview_bundled"):
        subprocess.run([sys.executable, str(root / "tests" / "office_package_smoke.py"), "--package-dir", str(package)], check=True)
    binaries = package if sys.platform == "win32" else package / "CampusPrint.app" / "Contents" / "MacOS"
    executable = binaries / ("print-supervisor.exe" if sys.platform == "win32" else "print-supervisor")
    env = {key: value for key, value in os.environ.items() if not key.startswith(("QT_", "QML", "DYLD_")) and key != "QTDIR"}
    env["PATH"] = str(Path(env.get("SystemRoot", "C:/Windows")) / "System32") if sys.platform == "win32" else "/usr/bin:/bin"
    subprocess.run([sys.executable, str(root / "tests" / "control_integration.py"),
                    "--supervisor", str(executable.resolve())], env=env, check=True)


if __name__ == "__main__":
    main()
