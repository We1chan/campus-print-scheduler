"""Use real Windows process objects to exercise the console-audit lifetime race."""
import ctypes
from ctypes import wintypes
import subprocess
import sys
import unittest
from unittest.mock import patch

from windows_desktop import _check_process_console


class ConsoleAuditTests(unittest.TestCase):
    def test_live_hidden_and_exited_processes(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        user = ctypes.WinDLL("user32", use_last_error=True)
        kernel.AttachConsole.argtypes = [wintypes.DWORD]
        kernel.AttachConsole.restype = kernel.FreeConsole.restype = wintypes.BOOL
        kernel.GetConsoleWindow.restype = wintypes.HWND
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        user.IsWindowVisible.restype = wintypes.BOOL
        self.assertTrue(kernel.FreeConsole())
        for console in (False, True):
            with self.subTest(hidden_console=console):
                startup = subprocess.STARTUPINFO()
                startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = subprocess.SW_HIDE
                process = subprocess.Popen([sys.executable, "-c", "import time; print('ready', flush=True); time.sleep(30)"],
                    startupinfo=startup, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                    creationflags=subprocess.CREATE_NEW_CONSOLE if console else subprocess.CREATE_NO_WINDOW)
                try:
                    self.assertEqual(process.stdout.readline(), b"ready\r\n")
                    self.assertTrue(_check_process_console(kernel, user, process.pid, "test-python"))
                    if console:
                        # Exercise the rejection on an actual attached console;
                        # keep the test window hidden on the user's desktop.
                        with patch.object(user, "IsWindowVisible", return_value=True):
                            with self.assertRaisesRegex(RuntimeError, "unwanted visible console"):
                                _check_process_console(kernel, user, process.pid, "test-python")
                    else:
                        # Access denied on a live target must remain a failure.
                        def denied(pid):
                            ctypes.set_last_error(5)
                            return False
                        with patch.object(kernel, "AttachConsole", side_effect=denied):
                            with self.assertRaises(OSError):
                                _check_process_console(kernel, user, process.pid, "test-python")
                finally:
                    process.terminate()
                    process.wait(timeout=10)
                    process.stdout.close()
                # Popen still owns the kernel process handle, so this is exactly
                # the CI race: the PID exists, but its process has exited.
                self.assertFalse(kernel.AttachConsole(process.pid))
                self.assertEqual(ctypes.get_last_error(), 5)
                self.assertFalse(_check_process_console(kernel, user, process.pid, "test-python"))


if __name__ == "__main__":
    unittest.main()
