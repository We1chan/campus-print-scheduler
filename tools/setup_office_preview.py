"""Prepare a private Windows LibreOffice image, without a system installation.

The MSI is pinned by SHA-256 (matching the upstream winget manifest). Extraction
uses Windows Installer's administrative-image mode, not /i. Other platforms use
their normal LibreOffice installation; see docs/office-preview.md.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import urllib.request

VERSION = "26.8.0"
SHA256 = "4aa6c6e1895f4055104effcb556bd3362d20c6ad707c149543304f395ef9db95"
FILENAME = "LibreOffice_26.8.0_Win_x86-64.msi"
URLS = ["https://download.documentfoundation.org/libreoffice/stable/" + VERSION + "/win/x86_64/" + FILENAME,
        "https://mirrors.tuna.tsinghua.edu.cn/libreoffice/libreoffice/stable/" + VERSION + "/win/x86_64/" + FILENAME]


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--output-dir", type=Path, default=root / ".tools" / "libreoffice")
    parser.add_argument("--installer", type=Path)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Private MSI extraction is Windows-only; install LibreOffice on macOS/Linux")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("--output-dir must not exist; preserve previous images")
    installer = args.installer
    if installer is None:
        downloads = root / ".tools" / "office-installer"
        downloads.mkdir(parents=True, exist_ok=True)
        installer = downloads / FILENAME
        if not installer.exists():
            partial = downloads / (FILENAME + ".part")
            errors = []
            for url in URLS:
                try:
                    with urllib.request.urlopen(url, timeout=30) as response, partial.open("wb") as target:
                        while chunk := response.read(1024 * 1024):
                            target.write(chunk)
                    with partial.open("rb") as source:
                        if hashlib.file_digest(source, "sha256").hexdigest() != SHA256:
                            raise ValueError("Installer SHA-256 mismatch")
                    partial.replace(installer)
                    break
                except Exception as error:
                    errors.append(str(error))
            if not installer.exists():
                raise RuntimeError("Unable to download verified installer: " + "; ".join(errors))
    installer = installer.resolve()
    with installer.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if actual != SHA256:
        parser.error("Installer SHA-256 mismatch; no executable was started")
    output.mkdir(parents=True)
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
