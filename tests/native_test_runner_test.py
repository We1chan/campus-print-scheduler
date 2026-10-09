"""Exercise the native runner's console boundary, without launching a desktop."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class NativeRunnerTests(unittest.TestCase):
    def test_legacy_windows_console_preserves_logs_and_failure(self):
        runner = Path(__file__).resolve().parents[1] / "tools/run_native_gui_tests.py"
        for status in (0, 1):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                (root / "merchant_tests.exe").touch()
                # Model a redirected Windows CI stdout using its legacy code page.
                # The real child process boundary must emit readable UTF-8 and keep
                # QtTest's failing exit code, even when both logs contain Chinese.
                driver = """
import pathlib, runpy, subprocess, sys, types
from unittest.mock import patch
sys.stdout.reconfigure(encoding='cp1252', errors='strict')
root = pathlib.Path(sys.argv[2])
status = int(sys.argv[3])
runner = sys.argv[1]
sys.argv = [runner, '--bin-dir', str(root), '--log-dir', str(root / 'logs')]
def run(command, **kwargs):
    report = pathlib.Path(command[-1].removesuffix(',txt'))
    report.write_text('QtTest: 中文 文档、取消中\\n', encoding='utf-8')
    return subprocess.CompletedProcess(command, status)
namespace = runpy.run_path(runner)
# Only the runner's platform decision is simulated. Changing the interpreter's
# sys.platform can make macOS standard-library imports demand Windows modules.
namespace['main'].__globals__['sys'] = types.SimpleNamespace(
    platform='win32', stdout=sys.stdout)
with patch('subprocess.run', run):
    namespace['main']()
"""
                result = subprocess.run([sys.executable, "-c", driver, str(runner),
                                         str(root), str(status)], capture_output=True, timeout=10)
                self.assertEqual(result.returncode, status, result.stderr.decode("utf-8", "replace"))
                self.assertEqual(result.stdout.decode("utf-8").count("中文 文档、取消中"), 2)
                self.assertTrue((root / "logs/native-gui-tests.txt").is_file())
                self.assertTrue((root / "logs/native-merchant-tests.txt").is_file())


if __name__ == "__main__":
    unittest.main()
