"""Verify six real Office formats through the production web preview engine."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    location = parser.add_mutually_exclusive_group(required=True)
    location.add_argument("--package-dir", type=Path)
    location.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    packaged = args.package_dir is not None
    root = (args.package_dir or args.source_root).resolve()
    if packaged:
        os.environ.pop("CPS_OFFICE_EXECUTABLE", None)
        fixtures = root / "examples" / "office"
    else:
        configured = os.environ.get("CPS_OFFICE_FIXTURES")
        if not configured:
            print("Real Office web preview skipped: CPS_OFFICE_FIXTURES is not configured.")
            return 77
        fixtures = Path(configured)
    sys.path.insert(0, str(root / ".tools" / "pdfium"))
    spec = importlib.util.spec_from_file_location("office_web", root / "tools" / "classroom_web.py")
    web = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(web)
    engine = web.office_executable()
    if engine is None:
        raise RuntimeError("Office preview engine missing")
    if packaged:
        expected = root / ("office/program/soffice.com" if sys.platform == "win32"
                           else "office/LibreOffice.app/Contents/MacOS/soffice")
        if engine.resolve() != expected.resolve():
            raise RuntimeError("Web preview did not discover the bundled Office engine")
    pdf = web.PdfEngine()
    if not pdf.available:
        raise RuntimeError("Web PDF renderer missing")
    converter = web.OfficeConverter(engine, lambda *unused: None)
    with tempfile.TemporaryDirectory(prefix="web-office-preview-") as directory:
        for extension in ("doc", "docx", "ppt", "pptx", "xls", "xlsx"):
            source = fixtures / ("中文 空格." + extension)
            original = hashlib.sha256(source.read_bytes()).digest()
            target = Path(directory) / (extension + ".pdf")
            error = converter.convert(source, "." + extension, target)
            if error:
                raise RuntimeError(extension + ": " + error)
            data = target.read_bytes()
            if pdf.pages(data) != 2:
                raise RuntimeError(extension + ": expected two real pages")
            first, last = pdf.render(data, 0, 480), pdf.render(data, 1, 480)
            if not first.startswith(b"\x89PNG") or not last.startswith(b"\x89PNG") or first == last:
                raise RuntimeError(extension + ": first/last page rendering failed")
            document = pdf.open(data)
            try:
                page = document[0]
                try:
                    width, height = page.get_size()
                finally:
                    page.close()
            finally:
                document.close()
            if extension.startswith("ppt") and width <= height:
                raise RuntimeError("PPT preview lost landscape direction")
            if hashlib.sha256(source.read_bytes()).digest() != original:
                raise RuntimeError("Office preview modified the source document")
            print(extension + ": 2 real pages rendered through the web engine", flush=True)
    if packaged and sys.platform == "darwin":
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict",
                        str(root / "office/LibreOffice.app")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
