"""Create a relocatable Qt distribution using the matching Qt deployment tool."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--qt-bin", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = args.bin_dir.resolve()
    output = args.output_dir.resolve()
    archive_format = "zip" if sys.platform == "win32" else "gztar"
    archive_path = Path(str(output) + (".zip" if sys.platform == "win32" else ".tar.gz"))
    # Never replace/delete a previous release or a user-selected existing directory.
    if output.exists() or archive_path.exists():
        parser.error("--output-dir must not exist; choose a new package directory")
    if sys.platform not in ("win32", "darwin"):
        parser.error("Packaging currently targets Windows and macOS")
    qt_bin = args.qt_bin.resolve()
    windows = sys.platform == "win32"
    suffix = ".exe" if windows else ""
    names = ["campus-print", "print-coordinator", "print-supervisor", "print-worker", "scheduler-replay", "shop-experiments"]
    deploy = qt_bin / ("windeployqt.exe" if windows else "macdeployqt")
    for path in [deploy, *(source / (name + suffix) for name in names)]:
        if not path.is_file():
            parser.error(f"Missing build/deployment input: {path}")
    output.mkdir(parents=True)
    bundle = output / "CampusPrint.app"
    destination = output if windows else bundle / "Contents" / "MacOS"
    destination.mkdir(parents=True, exist_ok=True)
    for name in names:
        shutil.copy2(source / (name + suffix), destination / (name + suffix))
    pdfium_name = "pdfium.dll" if windows else "libpdfium.dylib"
    pdfium = source / pdfium_name
    if not pdfium.is_file():
        parser.error("PDF preview renderer is missing; run tools/setup_pdfium.py, reconfigure and rebuild")
    shutil.copy2(pdfium, destination / pdfium_name)
    root = Path(__file__).resolve().parents[1]
    license_dirs = list((root / ".tools" / "pdfium").glob("pypdfium2-*.dist-info/licenses"))
    if not license_dirs:
        parser.error("PDFium redistribution licenses are missing from .tools/pdfium")
    shutil.copytree(license_dirs[0], output / "licenses" / "pdfium")
    env = dict(os.environ)
    env["PATH"] = str(qt_bin) + os.pathsep + env.get("PATH", "")
    if windows:
        for name in names:
            subprocess.run([str(deploy), "--no-translations", "--compiler-runtime", "--dir", str(output),
                            str(destination / (name + suffix))], env=env, check=True)
    else:
        (bundle / "Contents" / "Resources").mkdir()
        with (bundle / "Contents" / "Info.plist").open("wb") as file:
            plistlib.dump({"CFBundleExecutable": "campus-print", "CFBundleIdentifier": "edu.campusprint.scheduler",
                          "CFBundleName": "CampusPrint", "CFBundlePackageType": "APPL",
                          "CFBundleShortVersionString": "0.2.0", "NSHighResolutionCapable": True}, file)
        # Relocating dylib load commands invalidates the linker signature on arm64.
        # Sign the deployed binaries locally; this is not a Developer ID signature.
        subprocess.run([str(deploy), str(bundle), "-always-overwrite", "-codesign=-",
                        *("-executable=" + str(destination / name) for name in names[1:])], env=env, check=True)
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    root = Path(__file__).resolve().parents[1]
    shutil.copy2(root / "docs" / "release.md", output / "README-release.md")
    for name in ("student-handoff.md", "editorial-frontend.md", "interfaces.md", "team.md"):
        shutil.copy2(root / "docs" / name, output / name)
    # Keep the design evidence links usable in the redistributed document.
    image_output = output / "images"
    image_output.mkdir()
    for path in sorted((root / "docs" / "images").glob("editorial-*.png")):
        shutil.copy2(path, image_output / path.name)
    if windows:
        (output / "start-student.vbs").write_text('''Option Explicit
Dim fs, shell, root
Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
root = fs.GetParentFolderName(WScript.ScriptFullName)
shell.CurrentDirectory = root
shell.Run Chr(34) & fs.BuildPath(root, "campus-print.exe") & Chr(34) & " --role student", 1, False
''', encoding="ascii")
        shutil.copy2(root / "assets" / "editorial" / "paperroom.ico", output / "paperroom.ico")
    shutil.copy2(root / "docs" / "clean-up.md", output / "clean-up.md")
    shutil.copytree(root / "examples", output / "examples")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=root, capture_output=True, text=True, check=True).stdout.strip())
    files = [{"path": path.relative_to(output).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
             for path in sorted(output.rglob("*")) if path.is_file()]
    (output / "release-manifest.json").write_text(json.dumps(dict(schema_version=1, source_commit=commit,
        working_tree_dirty=dirty, platform=sys.platform, files=files), indent=2), encoding="utf-8")
    # A tarball preserves macOS executable permissions and framework symlinks.
    shutil.make_archive(str(output), archive_format, root_dir=output.parent, base_dir=output.name)
    print(output)


if __name__ == "__main__":
    main()
