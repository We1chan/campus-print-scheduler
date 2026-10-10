"""Use real Windows process objects to exercise the console-audit lifetime race."""
import ctypes
from ctypes import wintypes
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch

from windows_desktop import _check_process_console


class ConsoleAuditTests(unittest.TestCase):
    def test_denied_attach_waits_for_confirmed_exit(self):
        # Model the API boundary deterministically; the next test retains real
        # processes plus hidden-console and visible-console coverage.
        pid, handle = 4242, 1234
        for final_status, expected_error in ((0, None), (258, 5), (0xffffffff, 6)):
            with self.subTest(final_status=final_status):
                def denied(target):
                    self.assertEqual(target, pid)
                    ctypes.set_last_error(5)
                    return False

                def wait(target_handle, timeout_ms):
                    self.assertEqual(target_handle, handle)
                    if timeout_ms == 0:
                        return 258
                    self.assertEqual(timeout_ms, 250)
                    # The saved AttachConsole error must survive the wait.
                    ctypes.set_last_error(6 if final_status == 0xffffffff else 0)
                    return final_status

                kernel = SimpleNamespace(
                    OpenProcess=Mock(return_value=handle),
                    WaitForSingleObject=Mock(side_effect=wait),
                    AttachConsole=Mock(side_effect=denied),
                    CloseHandle=Mock(return_value=True))
                user = Mock()
                if expected_error is None:
                    self.assertFalse(_check_process_console(kernel, user, pid, "test-python"))
                else:
                    with self.assertRaises(OSError) as raised:
                        _check_process_console(kernel, user, pid, "test-python")
                    self.assertEqual(raised.exception.winerror, expected_error)
                    if expected_error == 5:
                        self.assertIn("test-python (4242)", str(raised.exception))
                        self.assertIn("250 ms", str(raised.exception))
                kernel.OpenProcess.assert_called_once_with(0x00100000, False, pid)
                self.assertEqual(kernel.WaitForSingleObject.call_args_list,
                                 [call(handle, 0), call(handle, 250)])
                kernel.AttachConsole.assert_called_once_with(pid)
                kernel.CloseHandle.assert_called_once_with(handle)
                user.IsWindowVisible.assert_not_called()

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
                # Popen still owns the kernel handle: this covers an already-
                # exited process whose object and PID still exist.
                self.assertFalse(kernel.AttachConsole(process.pid))
                self.assertEqual(ctypes.get_last_error(), 5)
                self.assertFalse(_check_process_console(kernel, user, process.pid, "test-python"))


if __name__ == "__main__":
    unittest.main()
