"""Windows release checks: real PE metadata and consoles on live backend processes."""
import ctypes
from ctypes import wintypes
import struct
import json
from pathlib import Path
import subprocess
import sys


def check_gui_binary(executable):
    data = executable.read_bytes()
    pe = struct.unpack_from("<I", data, 60)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional + 68)[0] != 2:
        raise RuntimeError("GUI executable uses the console subsystem")
    directory = optional + (112 if struct.unpack_from("<H", data, optional)[0] == 0x20B else 96)
    resource_rva = struct.unpack_from("<I", data, directory + 16)[0]
    sections = optional + struct.unpack_from("<H", data, pe + 20)[0]
    resource = None
    for index in range(struct.unpack_from("<H", data, pe + 6)[0]):
        size, rva, raw_size, raw = struct.unpack_from("<IIII", data, sections + index * 40 + 8)
        if rva <= resource_rva < rva + max(size, raw_size):
            resource = raw + resource_rva - rva
            break
    if resource is None:
        raise RuntimeError("GUI executable has no Windows resources")
    named, numbered = struct.unpack_from("<HH", data, resource + 12)
    types = [struct.unpack_from("<I", data, resource + 16 + index * 8)[0]
             for index in range(named + numbered)]
    if 14 not in types:  # RT_GROUP_ICON: used by Explorer and the Windows taskbar
        raise RuntimeError("GUI executable is missing its embedded app icon")


def check_backend_consoles(coordinator_pid):
    # Attaching a console changes standard handles. Keep that operation confined
    # to an isolated helper with redirected streams, not the package test runner.
    result = subprocess.run([sys.executable, str(Path(__file__).resolve()), str(coordinator_pid)],
                            stdin=subprocess.DEVNULL, capture_output=True,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return set(json.loads(result.stdout))


def _check_backend_consoles(coordinator_pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    user = ctypes.WinDLL("user32", use_last_error=True)
    # Detach only this test process, so AttachConsole's result describes the target.
    kernel.FreeConsole()
    kernel.AttachConsole.argtypes = [wintypes.DWORD]
    kernel.AttachConsole.restype = wintypes.BOOL
    kernel.GetConsoleWindow.restype = wintypes.HWND
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.IsWindowVisible.restype = wintypes.BOOL

    class ProcessEntry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG),
                    ("flags", wintypes.DWORD), ("exe", wintypes.WCHAR * 260)]

    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32FirstW.restype = kernel.Process32NextW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    processes = {}
    try:
        entry = ProcessEntry()
        entry.size = ctypes.sizeof(entry)
        available = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while available:
            processes[entry.pid] = (entry.parent, entry.exe)
            available = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    descendants = {coordinator_pid}
    while True:
        children = {pid for pid, (parent, _) in processes.items() if parent in descendants}
        if children <= descendants:
            break
        descendants |= children
    observed = set()
    for pid in descendants:
        name = processes.get(pid, (0, ""))[1]
        if name not in {"print-coordinator.exe", "print-supervisor.exe", "print-worker.exe"}:
            continue
        if kernel.AttachConsole(pid):
            window = kernel.GetConsoleWindow()
            visible = window and user.IsWindowVisible(window)
            kernel.FreeConsole()
            if visible:
                raise RuntimeError(f"{name} ({pid}) has an unwanted visible console")
            observed.add(name)
            continue
        error = ctypes.get_last_error()
        if error == 6:  # ERROR_INVALID_HANDLE: the running target has no console
            observed.add(name)
        elif error != 87:  # target may have exited since the process snapshot
            raise ctypes.WinError(error)
    return observed


if __name__ == "__main__":
    print(json.dumps(sorted(_check_backend_consoles(int(sys.argv[1])))))
