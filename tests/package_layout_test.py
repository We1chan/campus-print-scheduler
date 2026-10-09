"""Exercise the macOS deployment boundary without requiring a macOS host.

The deployment-tool double models the observed nested-framework rewrite;
real code-signing and six-format imports remain mandatory in macOS CI.
"""
import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


class PackageLayoutTests(unittest.TestCase):
    def test_qt_deployment_preserves_vendor_runtime(self):
        script = Path(__file__).resolve().parents[1] / "tools/package_app.py"
        spec = importlib.util.spec_from_file_location("package_app", script)
        app = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(app)
        with tempfile.TemporaryDirectory(prefix="paperroom-layout-") as directory:
            # macOS /var aliases /private/var; use the same canonical paths
            # as the production packager, including deployment-tool matching.
            root = Path(directory).resolve()
            source, qt, output = root / "bin", root / "qt", root / "release"
            office = root / "vendor/LibreOffice.app"
            for name in ("campus-print", "print-coordinator", "print-supervisor",
                         "print-worker", "scheduler-replay", "shop-experiments", "libpdfium.dylib"):
                source.mkdir(exist_ok=True)
                (source / name).write_bytes(b"binary fixture")
            qt.mkdir()
            (qt / "macdeployqt").touch()
            engine = office / "Contents/MacOS/soffice"
            engine.parent.mkdir(parents=True)
            engine.touch()
            resource = Path("Contents/Frameworks/LibreOfficePython.framework/sealed-resource")
            (office / resource).parent.mkdir(parents=True)
            (office / resource).write_bytes(b"vendor signature input")
            licenses = root / ".tools/pdfium/pypdfium2-fixture.dist-info/licenses"
            licenses.mkdir(parents=True)
            (licenses / "LICENSE").touch()
            (root / "docs/images").mkdir(parents=True)
            for document in script.parents[1].joinpath("docs").glob("*.md"):
                (root / "docs" / document.name).touch()
            (root / "examples").mkdir()
            fixtures = root / "fixtures"
            fixtures.mkdir()
            for suffix in ("doc", "docx", "ppt", "pptx", "xls", "xlsx"):
                (fixtures / ("中文 空格." + suffix)).touch()
            commands = []

            def run(command, **kwargs):
                commands.append(command)
                if command[0] == "/usr/bin/ditto":
                    shutil.copytree(command[1], command[2], symlinks=True)
                elif command[0] == str(qt / "macdeployqt"):
                    # Qt's recursive deployment can invalidate a vendor seal.
                    for nested in Path(command[1]).rglob("LibreOffice.app"):
                        (nested / resource).write_bytes(b"Qt rewrote the nested framework")
                elif command[0] == "/usr/bin/codesign":
                    for sealed in Path(command[-1]).rglob("sealed-resource"):
                        if sealed.read_bytes() != b"vendor signature input":
                            raise RuntimeError("a sealed resource is missing or invalid in LibreOfficePython.framework")
                stdout = "a" * 40 if command[:2] == ["git", "rev-parse"] else ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout)

            args = [str(script), "--bin-dir", str(source), "--qt-bin", str(qt),
                    "--output-dir", str(output), "--office-dir", str(office),
                    "--preview-fixtures", str(fixtures)]
            with patch.object(app, "__file__", str(root / "tools/package_app.py")), \
                    patch.object(app.sys, "platform", "darwin"), \
                    patch.object(app.sys, "argv", args), \
                    patch.object(app.subprocess, "run", side_effect=run), \
                    patch.object(app.shutil, "make_archive"), \
                    contextlib.redirect_stdout(io.StringIO()):
                app.main()
            copied = output / "office/LibreOffice.app"
            self.assertTrue((copied / resource).is_file())
            self.assertEqual((copied / resource).read_bytes(), (office / resource).read_bytes())
            self.assertFalse(list((output / "CampusPrint.app").rglob("LibreOffice.app")))
            verified = [Path(command[-1]) for command in commands if command[0] == "/usr/bin/codesign"]
            self.assertIn(office, verified)
            self.assertIn(copied, verified)
            self.assertIn(output / "CampusPrint.app", verified)


if __name__ == "__main__":
    unittest.main()
