"""Windows release checks: real PE metadata and consoles on live backend processes."""
import ctypes
from ctypes import wintypes
from collections import Counter
import struct
import json
from pathlib import Path
import subprocess
import sys


def _ico_frames(icon):
    data = icon.read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved or kind != 1 or not count:
        raise RuntimeError(f"Invalid expected ICO: {icon}")
    frames = []
    for index in range(count):
        width, height, colors, reserved, planes, bits, size, offset = struct.unpack_from(
            "<BBBBHHII", data, 6 + index * 16)
        payload = data[offset:offset + size]
        if reserved or not size or len(payload) != size:
            raise RuntimeError(f"Invalid expected ICO frame: {icon}")
        # windres canonically maps an unspecified ICO plane count to one.
        # All frame sizes, bit depths and full image bytes remain exact checks.
        frames.append((width or 256, height or 256, colors, planes or 1, bits, payload))
    return Counter(frames)


def check_gui_binary(executable, expected_icon=None):
    """Read PE resources without loading or executing the application.

    When supplied, expected_icon must match every embedded icon group, including
    each size's metadata and complete RT_ICON image bytes (PNG or DIB).
    """
    executable = Path(executable)
    data = executable.read_bytes()
    pe = struct.unpack_from("<I", data, 60)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional + 68)[0] != 2:
        raise RuntimeError("GUI executable uses the console subsystem")
    directory = optional + (112 if struct.unpack_from("<H", data, optional)[0] == 0x20B else 96)
    resource_rva = struct.unpack_from("<I", data, directory + 16)[0]
    sections = optional + struct.unpack_from("<H", data, pe + 20)[0]
    section_ranges = []
    for index in range(struct.unpack_from("<H", data, pe + 6)[0]):
        size, rva, raw_size, raw = struct.unpack_from("<IIII", data, sections + index * 40 + 8)
        section_ranges.append((rva, raw_size, raw))

    def file_offset(rva, size):
        for start, raw_size, raw in section_ranges:
            if start <= rva and rva + size <= start + raw_size:
                offset = raw + rva - start
                if offset + size <= len(data):
                    return offset
        raise RuntimeError("GUI executable has an invalid Windows resource address")

    if not resource_rva:
        raise RuntimeError("GUI executable has no Windows resources")
    resource = file_offset(resource_rva, 16)

    def entries(relative):
        offset = file_offset(resource_rva + relative, 16)
        named, numbered = struct.unpack_from("<HH", data, offset + 12)
        file_offset(resource_rva + relative, 16 + (named + numbered) * 8)
        return [struct.unpack_from("<II", data, offset + 16 + index * 8)
                for index in range(named + numbered)]

    types = dict(entries(0))
    if 14 not in types:  # RT_GROUP_ICON: used by Explorer and the Windows taskbar
        raise RuntimeError("GUI executable is missing its embedded app icon")
    if expected_icon is None:
        return

    def icon_resources(kind):
        if kind not in types or not types[kind] & 0x80000000:
            raise RuntimeError(f"GUI executable is missing its RT_{kind} icon resources")
        resources = {}
        for name, languages in entries(types[kind] & 0x7fffffff):
            if not languages & 0x80000000:
                raise RuntimeError("GUI executable has an invalid icon resource tree")
            for language, leaf in entries(languages & 0x7fffffff):
                if leaf & 0x80000000:
                    raise RuntimeError("GUI executable has an invalid icon resource leaf")
                offset = file_offset(resource_rva + leaf, 16)
                rva, size = struct.unpack_from("<II", data, offset)
                offset = file_offset(rva, size)
                resources[name, language] = data[offset:offset + size]
        return resources

    expected = _ico_frames(Path(expected_icon))
    icons = icon_resources(3)  # RT_ICON
    groups = icon_resources(14)
    if not groups:
        raise RuntimeError("GUI executable has no embedded icon groups")
    used = set()
    for (name, language), group in groups.items():
        reserved, kind, count = struct.unpack_from("<HHH", group)
        if reserved or kind != 1 or len(group) != 6 + count * 14:
            raise RuntimeError("GUI executable has an invalid embedded icon group")
        frames = []
        for index in range(count):
            width, height, colors, reserved, planes, bits, size, icon_id = struct.unpack_from(
                "<BBBBHHIH", group, 6 + index * 14)
            key = (icon_id, language)
            # A neutral image can be referenced by a localized group.
            if key not in icons:
                candidates = [candidate for candidate in icons if candidate[0] == icon_id]
                if len(candidates) != 1:
                    raise RuntimeError("GUI executable has an unresolved embedded icon frame")
                key = candidates[0]
            payload = icons[key]
            if reserved or len(payload) != size:
                raise RuntimeError("GUI executable has an invalid embedded icon frame size")
            used.add(key)
            frames.append((width or 256, height or 256, colors, planes, bits, payload))
        if Counter(frames) != expected:
            raise RuntimeError(f"{executable.name} embedded icon group {name} does not match "
                               f"{Path(expected_icon).name} (all sizes and image bytes required)")
    if used != set(icons):
        raise RuntimeError("GUI executable has unexpected unreferenced embedded icon frames")


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
    kernel.FreeConsole.restype = wintypes.BOOL
    if not kernel.FreeConsole():
        raise ctypes.WinError(ctypes.get_last_error())
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
        if _check_process_console(kernel, user, pid, name):
            observed.add(name)
    return observed


def _check_process_console(kernel, user, pid, name):
    # Keep the process object alive and check its signalled state. AttachConsole
    # returns ERROR_ACCESS_DENIED for an exited target whose object still exists,
    # not only when the caller is already attached. A stale snapshot must not be
    # treated as either a live console-free process or a permission failure.
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    process = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE only
    if not process:
        error = ctypes.get_last_error()
        if error == 87:  # no process object remains for the snapshot PID
            return False
        raise ctypes.WinError(error)

    def alive():
        status = kernel.WaitForSingleObject(process, 0)
        if status == 0:  # WAIT_OBJECT_0: exited, even if PID/object still exists
            return False
        if status != 258:  # WAIT_TIMEOUT: still running
            raise ctypes.WinError(ctypes.get_last_error())
        return True

    try:
        if not alive():
            return False
        if kernel.AttachConsole(pid):
            try:
                window = kernel.GetConsoleWindow()
                visible = window and user.IsWindowVisible(window)
            finally:
                if not kernel.FreeConsole():
                    raise ctypes.WinError(ctypes.get_last_error())
            if visible:
                raise RuntimeError(f"{name} ({pid}) has an unwanted visible console")
            return True
        error = ctypes.get_last_error()
        if not alive():  # target exited between liveness and console checks
            return False
        if error == 6:  # ERROR_INVALID_HANDLE: a live target has no console
            return True
        # Never waive access denied or other errors for a live target.
        raise ctypes.WinError(error)
    finally:
        kernel.CloseHandle(process)


if __name__ == "__main__":
    print(json.dumps(sorted(_check_backend_consoles(int(sys.argv[1])))))
