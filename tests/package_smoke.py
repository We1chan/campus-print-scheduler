"""Run deployed binaries with SDK paths removed; not a substitute for clean-OS acceptance."""
import argparse
from http.cookiejar import CookieJar
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import quote
from urllib.request import build_opener, HTTPCookieProcessor, ProxyHandler, Request


def sample_pdf():
    drawing = "0.8 0.2 0.1 rg 30 30 100 100 re f\n"
    objects = ["<< /Type /Catalog /Pages 2 0 R >>",
               "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
               "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>",
               f"<< /Length {len(drawing)} >>\nstream\n{drawing}endstream"]
    output, offsets = b"%PDF-1.4\n", []
    for index, body in enumerate(objects, 1):
        offsets.append(len(output))
        output += f"{index} 0 obj\n{body}\nendobj\n".encode()
    table = len(output)
    output += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
    output += b"".join(f"{offset:010d} 00000 n \n".encode() for offset in offsets)
    return output + f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{table}\n%%EOF\n".encode()


def verify_web_client(package, coordinator, state):
    """Exercise the packaged gateway against the same backend as the owner."""
    sys.path.insert(0, str(package / ".tools" / "pdfium"))
    spec = importlib.util.spec_from_file_location("packaged_web", package / "tools" / "classroom_web.py")
    web = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(web)
    application = web.Application(coordinator, state, "http://127.0.0.1", "PACKAGE")
    if not application.pdf.available:
        raise RuntimeError("Packaged web PDF renderer cannot be loaded")
    server = web.Server(("127.0.0.1", 0), application)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    client = build_opener(ProxyHandler({}), HTTPCookieProcessor(CookieJar()))
    base = f"http://127.0.0.1:{server.server_address[1]}"

    def read(path, body=None, headers=None):
        with client.open(Request(base + path, body, headers or {}), timeout=15) as response:
            return response.read()

    try:
        read("/join?code=PACKAGE")
        csrf = json.loads(read("/api/state"))["csrf"]
        for path in ("/", "/present", "/style.css", "/app.js", "/present.js", "/brand.png", "/favicon.png"):
            if not read(path):
                raise RuntimeError("Empty packaged web asset: " + path)
        upload = json.loads(read("/api/upload", sample_pdf(), {
            "X-CSRF-Token": csrf, "X-File-Name": quote("网页 中文任务.pdf"),
            "Content-Type": "application/octet-stream"}))
        if not read(f"/api/preview/{upload['upload_id']}/1?w=480").startswith(b"\x89PNG"):
            raise RuntimeError("Packaged web preview did not render")
        order = dict(upload_id=upload["upload_id"], pages=1, copies=1, paper="a4",
                     color=False, duplex=False, service="standard", payment="wechat")
        headers = {"X-CSRF-Token": csrf, "Content-Type": "application/json"}
        body = json.dumps(order).encode()
        if json.loads(read("/api/quote", body, headers))["quoted_cents"] <= 0:
            raise RuntimeError("Packaged web quote failed")
        submitted = json.loads(read("/api/submit", body, headers))
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            rows = application.backend.snapshot(fresh=True)["jobs"]
            row = next((item for item in rows if item["job_id"] == submitted["job_id"]), None)
            if row and row["state"] == "completed":
                break
            time.sleep(.1)
        else:
            raise RuntimeError("Packaged web order did not finish printing")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        application.pool.shutdown(wait=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    package = args.package_dir.resolve()
    windows = sys.platform == "win32"
    binaries = package if windows else package / "CampusPrint.app" / "Contents" / "MacOS"
    suffix = ".exe" if windows else ""
    owner_gui = binaries / ("campus-print" + suffix)
    if windows:
        from windows_desktop import check_gui_binary, check_backend_consoles
        check_gui_binary(owner_gui, expected_icon=package / "paperroom-merchant-v3.ico")
        merchant_gui = binaries / "campus-print-merchant.exe"
        if merchant_gui.is_file():
            owner_gui = merchant_gui
            check_gui_binary(owner_gui, expected_icon=package / "paperroom-merchant-v3.ico")
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("QT_", "QML", "DYLD_")) and key not in ("QTDIR", "QT_PLUGIN_PATH")}
    env["PATH"] = str(Path(env.get("SystemRoot", "C:/Windows")) / "System32") if windows else "/usr/bin:/bin"
    # Validate the platform plugin shipped to end users, not an SDK-only test plugin.
    env["QT_QPA_PLATFORM"] = "windows" if windows else "cocoa"
    with tempfile.TemporaryDirectory(prefix="package-test-") as temp:
        root = Path(temp) / "中文 空格路径"
        root.mkdir()
        result = subprocess.run([str(owner_gui), "--smoke-test", "--data-dir", str(root)],
                                env=env, cwd=root, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Deployed GUI failed ({result.returncode}): {result.stderr!r}")
        coordinator = binaries / ("print-coordinator" + suffix)
        def request(command):
            response = subprocess.run([str(coordinator), "--state-dir", str(root),
                                       "--request", json.dumps(command)], env=env, cwd=root,
                                      capture_output=True, encoding="utf-8", timeout=12)
            if response.returncode:
                raise RuntimeError(f"Deployed coordinator request failed: {response.stderr} {response.stdout}")
            return json.loads(response.stdout)
        try:
            request(dict(type="set_paused", paused=True))
            request(dict(type="submit_batch", jobs=[
                dict(job_id="persist-a", title="发布包持久队列", pages=20, copies=1),
                dict(job_id="persist-b", title="关闭后恢复", pages=2, copies=1)]))
            # The first GUI already exited. A second GUI must attach to the same queue.
            again = subprocess.run([str(owner_gui), "--smoke-test",
                                    "--data-dir", str(root)], env=env, cwd=root, capture_output=True, timeout=20)
            if again.returncode:
                raise RuntimeError(f"Deployed GUI reopen failed: {again.stderr!r}")
            snapshot = request(dict(type="snapshot"))
            if len(snapshot["jobs"]) != 2 or not snapshot["paused"]:
                raise RuntimeError("Deployed GUI failed to preserve the shared queue/pause state")
            request(dict(type="set_paused", paused=False))
            deadline = time.monotonic() + 15
            observed = set()
            while time.monotonic() < deadline:
                snapshot = request(dict(type="snapshot"))
                if windows:
                    observed |= check_backend_consoles(snapshot["coordinator_pid"])
                if snapshot["metrics"]["completed"] == 2:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Deployed coordinator did not finish both queued jobs")
            if windows and observed != {"print-coordinator.exe", "print-supervisor.exe", "print-worker.exe"}:
                raise RuntimeError(f"Console audit did not cover the complete backend: {observed}")
            for item in snapshot["jobs"]:
                output = root / "output" / (item["job_id"] + ".txt")
                if item["attempts"] != 1 or output.read_text(encoding="utf-8").count("sheet=") != item["pages"]:
                    raise RuntimeError("Deployed persistent job duplicated or produced incorrect output")
            verify_web_client(package, coordinator, root)
        finally:
            request(dict(type="shutdown"))
            deadline = time.monotonic() + 5
            while (root / ".coordinator.lock").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            if (root / ".coordinator.lock").exists():
                raise RuntimeError("Deployed coordinator did not shut down")
        job = root / "任务.json"
        job.write_text(json.dumps(dict(schema_version=1, job_id="package-job", title="发布包测试", pages=2, copies=1),
                                  ensure_ascii=False), encoding="utf-8")
        result = subprocess.run([str(binaries / ("print-supervisor" + suffix)), "--job", str(job),
                                 "--output-dir", str(root), "--page-ms", "10"],
                                env=env, cwd=root, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Deployed supervisor failed ({result.returncode}): {result.stderr!r}")
        events = [json.loads(line) for line in result.stdout.decode("utf-8").splitlines()]
        if not events or events[-1]["type"] != "supervisor_completed":
            raise RuntimeError("Deployed supervisor did not report completion")
        content = (root / "package-job.txt").read_text(encoding="utf-8")
        if content.count("sheet=") != 2:
            raise RuntimeError("Deployed worker output does not match the requested job")
    print("Deployed owner GUI, student web client, persistent queue and supervisor/worker printing passed with SDK paths removed.")
    if windows:
        print("Windows GUI subsystem, all embedded app icon sizes/bytes and console-free coordinator/supervisor/worker verified.")


if __name__ == "__main__":
    main()
