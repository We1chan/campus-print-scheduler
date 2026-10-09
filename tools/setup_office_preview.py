"""Prepare a private LibreOffice image, without a system installation.

The MSI is pinned by SHA-256 (matching the upstream winget manifest). Extraction
uses Windows Installer's administrative-image mode, not /i. macOS copies the
verified application from a read-only DMG; Linux uses its distribution package.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import plistlib
import subprocess
import sys
import time
import urllib.request

VERSION = "26.8.0"
SHA256 = "4aa6c6e1895f4055104effcb556bd3362d20c6ad707c149543304f395ef9db95"
FILENAME = "LibreOffice_26.8.0_Win_x86-64.msi"
MAC_DIGESTS = {"arm64": ("aarch64", "aarch64", "8858d8058da4f862f47559486814e65efc27294da67c5e4bb56b006b1ee59f89"),
               "x86_64": ("x86_64", "x86-64", "2dcbce4894e01bc1ecd594658e2cbda70ff7bfcd0b310f35d38887797172d09e")}


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--output-dir", type=Path, default=root / ".tools" / "libreoffice")
    parser.add_argument("--installer", type=Path)
    args = parser.parse_args()
    if sys.platform not in ("win32", "darwin"):
        parser.error("Private extraction targets Windows/macOS; install LibreOffice on Linux")
    digest, filename, relative = SHA256, FILENAME, "win/x86_64/" + FILENAME
    if sys.platform == "darwin":
        architecture = MAC_DIGESTS.get(platform.machine())
        if architecture is None:
            parser.error("Unsupported macOS architecture")
        folder, arch, digest = architecture
        filename = "LibreOffice_" + VERSION + "_MacOS_" + arch + ".dmg"
        relative = "mac/" + folder + "/" + filename
    urls = ["https://download.documentfoundation.org/libreoffice/stable/" + VERSION + "/" + relative,
            "https://mirrors.tuna.tsinghua.edu.cn/libreoffice/libreoffice/stable/" + VERSION + "/" + relative]
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("--output-dir must not exist; preserve previous images")
    installer = args.installer
    if installer is None:
        downloads = root / ".tools" / "office-installer"
        downloads.mkdir(parents=True, exist_ok=True)
        installer = downloads / filename
        if not installer.exists():
            partial = downloads / (filename + ".part")
            errors = []
            for url in urls:
                try:
                    started = time.monotonic()
                    print("Downloading verified Office engine:", url, flush=True)
                    with urllib.request.urlopen(url, timeout=30) as response, partial.open("wb") as target:
                        while chunk := response.read(1024 * 1024):
                            if time.monotonic() - started > 240:
                                raise TimeoutError("Installer download exceeded four minutes")
                            target.write(chunk)
                    with partial.open("rb") as source:
                        if hashlib.file_digest(source, "sha256").hexdigest() != digest:
                            raise ValueError("Installer SHA-256 mismatch")
                    partial.replace(installer)
                    break
                except Exception as error:
                    errors.append(str(error))
                    print("Download failed; checking next verified source:", str(error), flush=True)
            if not installer.exists():
                raise RuntimeError("Unable to download verified installer: " + "; ".join(errors))
    installer = installer.resolve()
    with installer.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if actual != digest:
        parser.error("Installer SHA-256 mismatch; no executable was started")
    output.mkdir(parents=True)
    if sys.platform == "darwin":
        # Hashes match the Homebrew cask; mounting doesn't launch the document
        # converter or modify /Applications, PATH, or the user's Office profile.
        mounted = subprocess.run(["hdiutil", "attach", str(installer), "-readonly", "-nobrowse", "-plist"],
                                 capture_output=True, check=True, timeout=90)
        entities = plistlib.loads(mounted.stdout)["system-entities"]
        mounts = [Path(entry["mount-point"]) for entry in entities if "mount-point" in entry]
        try:
            app = next((mount / "LibreOffice.app" for mount in mounts if (mount / "LibreOffice.app").is_dir()), None)
            if app is None:
                raise RuntimeError("Verified DMG contains no LibreOffice.app")
            subprocess.run(["ditto", str(app), str(output / "LibreOffice.app")], check=True, timeout=120)
        finally:
            for mount in mounts:
                subprocess.run(["hdiutil", "detach", str(mount)], check=True, timeout=30)
        engine = output / "LibreOffice.app/Contents/MacOS/soffice"
        if not engine.is_file() or not os.access(engine, os.X_OK):
            raise RuntimeError("Expected one executable LibreOffice application")
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(output / "LibreOffice.app")], check=True)
        print(engine)
        print("Package engine root:", engine.parents[2])
        return
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    subprocess.run(["msiexec.exe", "/a", str(installer), "/qn", "TARGETDIR=" + str(output),
                    "/L*v", str(output / "extraction.log")], startupinfo=startup,
                   creationflags=subprocess.CREATE_NO_WINDOW, check=True, timeout=240)
    programs = list(output.rglob("soffice.com"))
    if len(programs) != 1:
        raise RuntimeError("Expected one extracted LibreOffice executable")
    print(programs[0])
    print("Package engine root:", programs[0].parent.parent)


if __name__ == "__main__":
    main()
