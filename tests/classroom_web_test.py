#!/usr/bin/env python3
"""HTTP -> real coordinator -> worker tests, including browser isolation."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
from http.cookiejar import CookieJar
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import build_opener, HTTPCookieProcessor, HTTPRedirectHandler, ProxyHandler, Request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("classroom_web", ROOT / "tools" / "classroom_web.py")
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)
EXECUTABLE = None
OFFICE_STUB = None


def make_pdf(sizes):
    """A minimal valid PDF with one page per (width, height) in points."""
    objects = ["<< /Type /Catalog /Pages 2 0 R >>",
               "<< /Type /Pages /Kids [%s] /Count %d >>" % (" ".join(f"{3 + i} 0 R" for i in range(len(sizes))), len(sizes))]
    objects += [f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}] >>" for w, h in sizes]
    output, offsets = b"%PDF-1.4\n", []
    for index, body in enumerate(objects, 1):
        offsets.append(len(output))
        output += f"{index} 0 obj\n{body}\nendobj\n".encode()
    table = len(output)
    output += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
    output += b"".join(f"{offset:010d} 00000 n \n".encode() for offset in offsets)
    return output + f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{table}\n%%EOF\n".encode()


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


class Client:
    def __init__(self, base, join=True):
        self.base = base
        self.jar = CookieJar()
        self.opener = build_opener(ProxyHandler({}), HTTPCookieProcessor(self.jar))
        self.csrf = ""
        if join:
            self.raw("/join?code=CLASSROOM")
            self.csrf = self.json("/api/state")[1]["csrf"]

    def raw(self, path, body=None, headers=None):
        request = Request(self.base + path, body, headers or {})
        try:
            with self.opener.open(request, timeout=30) as response:
                return response.status, response.read()
        except HTTPError as response:
            with response:
                return response.code, response.read()

    def json(self, path, body=None, csrf=True):
        headers = {"Content-Type": "application/json"}
        if csrf:
            headers["X-CSRF-Token"] = self.csrf
        code, data = self.raw(path, json.dumps(body).encode() if body is not None else None, headers)
        return code, json.loads(data)

    def upload(self, name="实验报告.txt", data=b"hello\n" * 101):
        code, result = self.raw("/api/upload", data, {"X-CSRF-Token": self.csrf,
                               "X-File-Name": quote(name), "Content-Type": "application/octet-stream"})
        return code, json.loads(result)

    def order(self, upload, **options):
        return dict(upload_id=upload["upload_id"], pages=upload.get("pages", 3) if upload.get("pages_locked") else 3,
                    copies=2, paper="a3", color=True, duplex=True, service="flexible", payment="wechat", **options)


class ClassroomTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ready = False
        cls.directory = tempfile.TemporaryDirectory(prefix="课堂 扫码-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.state = Path(cls.directory.name)
        cls.log = (cls.state / "test-coordinator.log").open("wb")
        cls.addClassCleanup(cls.log.close)
        cls.process = subprocess.Popen([str(EXECUTABLE), "--state-dir", str(cls.state), "--shop-mode", "--page-ms", "10"],
                                       stdout=cls.log, stderr=cls.log,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        # Class cleanups also run when setUpClass fails, unlike tearDownClass.
        cls.addClassCleanup(cls.stop_coordinator)
        # The converter stub copies a fixture PDF; without it Office falls back to manual pages.
        cls.environment = os.environ.get("CPS_OFFICE_EXECUTABLE")
        cls.addClassCleanup(cls.restore_environment)
        os.environ["CPS_OFFICE_EXECUTABLE"] = str(OFFICE_STUB) if OFFICE_STUB else "missing-converter"
        cls.app = web.Application(EXECUTABLE, cls.state, "http://127.0.0.1", "CLASSROOM")
        cls.addClassCleanup(cls.app.pool.shutdown, wait=True)
        deadline = time.monotonic() + 15
        while True:
            try:
                cls.app.backend.snapshot(fresh=True)
                break
            except web.Problem as error:
                if cls.process.poll() is not None or time.monotonic() > deadline:
                    try:
                        probe = subprocess.run([str(EXECUTABLE), "--state-dir", str(cls.app.state),
                                                "--request", '{"type":"snapshot"}'], capture_output=True,
                                               timeout=10, creationflags=web.NO_WINDOW)
                        detail = (f"request_exit={probe.returncode}, stdout={probe.stdout[-2000:]!r}, "
                                  f"stderr={probe.stderr[-2000:]!r}")
                    except (OSError, subprocess.TimeoutExpired) as probe_error:
                        detail = f"diagnostic request failed: {probe_error}"
                    log = (cls.state / "test-coordinator.log").read_text(encoding="utf-8", errors="replace")
                    raise RuntimeError(f"Coordinator startup failed: service_exit={cls.process.poll()}, "
                                       f"{detail}, service_log={log[-4000:]}") from error
                time.sleep(.05)
        cls.app.backend.request("set_paused", paused=True)
        cls.app.backend.invalidate()
        cls.server = web.Server(("127.0.0.1", 0), cls.app)
        cls.addClassCleanup(cls.server.server_close)
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"
        cls.app.public_url = cls.base
        cls.app.join_url = cls.base + "/join?code=CLASSROOM"
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.addClassCleanup(cls.server.shutdown)
        cls.ready = True

    @classmethod
    def restore_environment(cls):
        if cls.environment is None:
            os.environ.pop("CPS_OFFICE_EXECUTABLE", None)
        else:
            os.environ["CPS_OFFICE_EXECUTABLE"] = cls.environment

    @classmethod
    def stop_coordinator(cls):
        try:
            if cls.process.poll() is None and hasattr(cls, "app"):
                cls.app.backend.request("set_paused", paused=False)
                cls.app.backend.request("shutdown")
                cls.process.wait(timeout=15)
        except (web.Problem, subprocess.TimeoutExpired):
            if cls.ready:
                raise
        finally:
            if cls.process.poll() is None:
                cls.process.kill()
                cls.process.wait(timeout=5)

    def setUp(self):
        self.client = Client(self.base)

    @unittest.skipUnless(os.name == "nt", "Windows DOS path aliases")
    def test_windows_short_and_long_state_paths_share_backend(self):
        import ctypes
        from ctypes import wintypes

        short_path = ctypes.WinDLL("kernel32", use_last_error=True).GetShortPathNameW
        short_path.argtypes = (wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD)
        short_path.restype = wintypes.DWORD
        long_state = str(self.state.resolve())
        required = short_path(long_state, None, 0)
        self.assertGreater(required, 0, "GetShortPathNameW failed")
        buffer = ctypes.create_unicode_buffer(required)
        length = short_path(long_state, buffer, required)
        self.assertGreater(length, 0)
        self.assertLess(length, required)
        if buffer.value.casefold() == long_state.casefold():
            self.skipTest("The test volume does not provide a DOS 8.3 alias")
        # The same real coordinator must be reachable through both spellings.
        short_snapshot = web.Backend(EXECUTABLE, buffer.value).snapshot(fresh=True)
        long_snapshot = web.Backend(EXECUTABLE, long_state).snapshot(fresh=True)
        self.assertEqual(short_snapshot["coordinator_pid"], self.process.pid)
        self.assertEqual(long_snapshot["coordinator_pid"], self.process.pid)

    def test_access_and_csrf(self):
        outsider = Client(self.base, join=False)
        self.assertEqual(outsider.json("/api/state")[0], 401)
        status, page = outsider.raw("/")
        self.assertEqual(status, 200)
        self.assertIn("课堂码".encode(), page)
        self.assertEqual(outsider.raw("/app.js")[0], 401)
        self.assertEqual(outsider.raw("/style.css")[0], 200)
        status, page = outsider.raw("/join?code=WRONG")
        self.assertEqual(status, 403)
        self.assertIn("课堂码无效".encode(), page)
        # The join form accepts a typed code regardless of letter case.
        typed = build_opener(ProxyHandler({}), NoRedirect())
        try:
            typed.open(self.base + "/join?code=classroom", timeout=10)
            self.fail("expected a redirect")
        except HTTPError as response:
            with response:
                self.assertEqual(response.code, 303)
                self.assertIn("classroom=", response.headers["Set-Cookie"])
        self.assertEqual(self.client.json("/api/submit", {}, csrf=False)[0], 403)
        for path in ("/", "/present", "/style.css", "/app.js", "/present.js", "/brand.png", "/favicon.png"):
            self.assertEqual(self.client.raw(path)[0], 200, path)
        self.assertEqual(self.client.json("/api/set_policy", {"policy": "sjf"})[0], 404)
        self.assertEqual(self.client.raw("/../state.json")[0], 404)
        private = web.Application(EXECUTABLE, self.state, "http://192.168.137.1:8787", "CLASSROOM")
        self.assertIn("同一", private.access_hint)
        secure = web.Application(EXECUTABLE, self.state, "https://print.example.com", "CLASSROOM")
        self.assertIn("; Secure", secure.cookie("a" * 32))

    def test_upload_quote_submit_and_reconcile(self):
        status, uploaded = self.client.upload()
        self.assertEqual(status, 201)
        self.assertEqual((uploaded["pages"], uploaded["pages_locked"], uploaded["preview"]), (3, False, "text"))
        self.assertNotIn("customer", uploaded)
        binary = self.app.uploads / (uploaded["upload_id"] + ".bin")
        self.assertEqual(binary.read_bytes(), b"hello\n" * 101)
        order = self.client.order(uploaded)
        order.update(quoted_cents=1, customer="another-student", priority=100, job_id="forged")
        status, result = self.client.json("/api/quote", order)
        self.assertEqual(status, 200)
        self.assertEqual(result["quoted_cents"], 1454)
        self.assertEqual(result["paper_sheets"], 4)
        self.assertIn("estimated_finish_ms", result)
        # Every tier is priced by the backend; slower tiers are cheaper.
        tiers = result["tiers"]
        self.assertEqual(tiers["flexible"], 1454)
        self.assertGreater(tiers["express"], tiers["standard"])
        self.assertGreater(tiers["standard"], tiers["flexible"])
        before = len(self.app.backend.snapshot(fresh=True)["jobs"])
        status, submitted = self.client.json("/api/submit", order)
        self.assertEqual(status, 200)
        self.assertEqual(submitted["job_id"], web.job_id(uploaded["upload_id"]))
        self.assertEqual(submitted["pickup_code"], uploaded["upload_id"][:6].upper())
        # Same stable id after a lost reply cannot enqueue a second job.
        self.assertEqual(self.client.json("/api/submit", order)[0], 200)
        snapshot = self.app.backend.snapshot(fresh=True)
        self.assertEqual(len(snapshot["jobs"]), before + 1)
        row = next(row for row in snapshot["jobs"] if row["job_id"] == submitted["job_id"])
        self.assertNotEqual(row["customer"], "another-student")
        self.assertTrue(row["customer"].startswith("访客 "))
        self.assertEqual(row["priority"], 0)
        self.assertEqual(row["quoted_cents"], 1454)
        order["pages"] = 4
        self.assertEqual(self.client.json("/api/submit", order)[0], 409)
        # Persisted HMAC key and upload record survive gateway restart.
        replacement = web.Application(EXECUTABLE, self.state, self.base, "CLASSROOM")
        cookie = "; ".join(f"{item.name}={item.value}" for item in self.client.jar)
        session = replacement.session(cookie)
        self.assertIsNotNone(session)
        self.assertEqual(replacement.metadata(uploaded["upload_id"], session)["name"], "实验报告.txt")
        mine = replacement.state_for(session)["jobs"]
        self.assertEqual([job["job_id"] for job in mine], [submitted["job_id"]])
        self.assertEqual(mine[0]["pickup_code"], submitted["pickup_code"])
        self.assertEqual(mine[0]["sheets"], 6)

    def test_multi_file_merged_order(self):
        _, first = self.client.upload("封面.pdf", make_pdf([(595, 842), (595, 842)]))
        _, second = self.client.upload("正文.pdf", make_pdf([(595, 842), (595, 842), (595, 842)]))
        self.assertTrue(first["pages_locked"] and second["pages_locked"])
        batch = [{"upload_id": first["upload_id"], "pages": 2}, {"upload_id": second["upload_id"], "pages": 3}]
        base = dict(documents=batch, copies=1, paper="a4", color=False, duplex=False, service="standard", payment="wechat")
        self.assertEqual(self.client.json("/api/quote", dict(base))[0], 200)
        status, submitted = self.client.json("/api/submit", dict(base))
        self.assertEqual(status, 200)
        self.assertEqual(submitted["job_id"], web.job_id(first["upload_id"]))
        self.assertEqual(submitted["pickup_code"], first["upload_id"][:6].upper())
        row = next(row for row in self.app.backend.snapshot(fresh=True)["jobs"] if row["job_id"] == submitted["job_id"])
        self.assertEqual(row["pages"], 5)
        self.assertEqual(row["title"], "封面.pdf 等2份")
        # A locked file's page count cannot be padded in the merged total.
        self.assertEqual(self.client.json("/api/submit", {**base, "documents": [
            {"upload_id": first["upload_id"], "pages": 2}, {"upload_id": second["upload_id"], "pages": 4}]})[0], 422)
        # Duplicate files in one batch are rejected.
        self.assertEqual(self.client.json("/api/submit", {**base, "documents": [
            {"upload_id": first["upload_id"], "pages": 2}, {"upload_id": first["upload_id"], "pages": 2}]})[0], 422)
        # A file the session does not own is invisible to the batch.
        outsider = Client(self.base)
        self.assertEqual(outsider.json("/api/submit", {**base, "documents": [
            {"upload_id": first["upload_id"], "pages": 2}, {"upload_id": "f" * 32, "pages": 1}]})[0], 404)
        # Resubmitting the identical batch reconciles instead of double-ordering.
        self.assertEqual(self.client.json("/api/submit", dict(base))[0], 200)

    def test_display_name_and_quote_before_upload(self):
        status, quoted = self.client.json("/api/quote", dict(pages=8, copies=1, paper="a4", color=True, duplex=True,
                                                             service="standard", payment="wechat"))
        self.assertEqual(status, 200)
        self.assertEqual(quoted["type"], "quote")
        self.assertEqual(set(quoted["tiers"]), {"express", "standard", "flexible"})
        # A submit always needs an uploaded file.
        self.assertEqual(self.client.json("/api/submit", dict(pages=8, copies=1, paper="a4", color=True, duplex=True,
                                                              service="standard", payment="wechat"))[0], 404)
        _, uploaded = self.client.upload("署名.txt")
        self.assertEqual(self.client.json("/api/quote", self.client.order(uploaded, student="x" * 33))[0], 422)
        _, submitted = self.client.json("/api/submit", self.client.order(uploaded, student=" 2024211444 "))
        row = next(row for row in self.app.backend.snapshot(fresh=True)["jobs"] if row["job_id"] == submitted["job_id"])
        self.assertEqual(row["customer"], "2024211444")
        # Ownership follows the browser session, not the typed name.
        impostor = Client(self.base)
        self.assertEqual(impostor.json("/api/state")[1]["jobs"], [])
        self.assertEqual(impostor.json("/api/cancel", {"job_id": submitted["job_id"]})[0], 404)

    def test_private_orders_and_cancel(self):
        _, uploaded = self.client.upload("私人文件.txt")
        _, order = self.client.json("/api/submit", self.client.order(uploaded))
        other = Client(self.base)
        status, other_state = other.json("/api/state")
        self.assertEqual(status, 200)
        self.assertEqual(other_state["jobs"], [])
        self.assertNotIn("私人文件", json.dumps(other_state, ensure_ascii=False))
        self.assertTrue(any(not row["own"] and row["title"] is None for row in other_state["queue"]))
        self.assertTrue(any(row["own"] and row["title"] == "私人文件.txt" for row in self.client.json("/api/state")[1]["queue"]))
        self.assertEqual(other.json("/api/quote", other.order(uploaded))[0], 404)
        self.assertEqual(other.json("/api/cancel", {"job_id": order["job_id"]})[0], 404)
        self.assertEqual(other.raw(f"/api/upload/{uploaded['upload_id']}")[0], 404)
        self.assertEqual(self.client.json("/api/cancel", {"job_id": order["job_id"]})[0], 200)
        _, mine = self.client.json("/api/state")
        self.assertEqual(mine["jobs"][0]["state"], "cancelled")
        self.assertNotIn("customer", mine["jobs"][0])
        self.assertNotIn("runs", mine["jobs"][0])
        self.assertEqual(self.client.raw("/uploads/" + uploaded["upload_id"] + ".bin")[0], 404)

    def test_invalid_uploads_and_parameters(self):
        for name in ("../x.pdf", "C:\\x.pdf", "x.exe", "x\n.pdf"):
            self.assertEqual(self.client.upload(name)[0], 422)
        self.assertEqual(self.client.upload(data=b"")[0], 413)
        self.assertEqual(self.client.upload("伪装.docx", b"plain text")[0], 422)
        _, uploaded = self.client.upload()
        valid = self.client.order(uploaded)
        for key, value in (("pages", True), ("pages", 0), ("copies", 101), ("paper", []), ("color", "false")):
            bad = {**valid, key: value}
            self.assertEqual(self.client.json("/api/quote", bad)[0], 422)
        self.assertEqual(self.client.json("/api/quote", {**valid, "pages": 1000, "copies": 100})[0], 422)
        # An image prints exactly one page; the page count cannot be changed.
        status, image = self.client.upload("照片.png", b"\x89PNG\r\n\x1a\n" + b"0" * 64)
        self.assertEqual((status, image["pages"], image["pages_locked"]), (201, 1, True))
        self.assertEqual(self.client.json("/api/quote", {**self.client.order(image), "pages": 3})[0], 422)
        self.assertEqual(self.client.json("/api/quote", self.client.order(image))[0], 200)
        cookie = "; ".join(f"{item.name}={item.value}" for item in self.client.jar)
        conn = http.client.HTTPConnection("127.0.0.1", self.server.server_address[1], timeout=5)
        conn.putrequest("POST", "/api/upload")
        conn.putheader("Cookie", cookie)
        conn.putheader("X-CSRF-Token", self.client.csrf)
        conn.putheader("Content-Length", str(web.MAX_FILE + 1))
        conn.endheaders()
        self.assertEqual(conn.getresponse().status, 413)
        conn.close()

    def test_pdf_pages_and_preview(self):
        if not self.app.pdf.available:
            self.skipTest("PDFium not installed; run tools/setup_pdfium.py")
        status, uploaded = self.client.upload("讲义 第一章.pdf", make_pdf([(595, 842), (595, 842), (842, 595)]))
        self.assertEqual(status, 201)
        self.assertEqual((uploaded["pages"], uploaded["pages_locked"], uploaded["preview"]), (3, True, "pdf"))
        status, image = self.client.raw(f"/api/preview/{uploaded['upload_id']}/1?w=480")
        self.assertEqual(status, 200)
        self.assertTrue(image.startswith(b"\x89PNG\r\n\x1a\n"))
        width, height = struct.unpack(">II", image[16:24])
        self.assertEqual(width, 480)
        self.assertGreater(height, width)
        # A landscape page stays landscape, and the second read comes from the cache.
        status, landscape = self.client.raw(f"/api/preview/{uploaded['upload_id']}/3?w=960")
        self.assertEqual(status, 200)
        width, height = struct.unpack(">II", landscape[16:24])
        self.assertEqual(width, 960)
        self.assertLess(height, width)
        self.assertEqual(self.client.raw(f"/api/preview/{uploaded['upload_id']}/3?w=960")[1], landscape)
        for path in (f"/api/preview/{uploaded['upload_id']}/4?w=480", f"/api/preview/{uploaded['upload_id']}/1?w=123"):
            self.assertEqual(self.client.raw(path)[0], 404)
        self.assertEqual(Client(self.base).raw(f"/api/preview/{uploaded['upload_id']}/1?w=480")[0], 404)
        self.assertEqual(self.client.json("/api/quote", {**self.client.order(uploaded), "pages": 2})[0], 422)
        self.assertEqual(self.client.json("/api/quote", self.client.order(uploaded))[0], 200)
        status, problem = self.client.upload("损坏.pdf", b"%PDF-1.4 not really")
        self.assertEqual(status, 422)
        self.assertIn("PDF", problem["error"])

    def test_document_endpoint_previews_images_and_text(self):
        png = b"\x89PNG\r\n\x1a\n" + b"0" * 64
        _, image = self.client.upload("照片.png", png)
        status, body = self.client.raw(f"/api/document/{image['upload_id']}")
        self.assertEqual((status, body), (200, png))
        _, note = self.client.upload("笔记.txt", b"first\nsecond\n")
        status, body = self.client.raw(f"/api/document/{note['upload_id']}")
        self.assertEqual((status, body), (200, b"first\nsecond\n"))
        # Only the owning session may read the stored document.
        outsider = Client(self.base)
        self.assertEqual(outsider.raw(f"/api/document/{image['upload_id']}")[0], 404)
        # PDFs preview through rendered pages, never through the raw endpoint.
        _, pdf = self.client.upload("讲义.pdf", make_pdf([(595, 842)]))
        self.assertEqual(self.client.raw(f"/api/document/{pdf['upload_id']}")[0], 404)
        self.assertEqual(self.client.raw("/api/document/" + "f" * 32)[0], 404)

    def test_office_conversion(self):
        if not self.app.office.available:
            # Without a converter the student types the page count.
            status, uploaded = self.client.upload("报告.docx", b"PK\x03\x04" + b"0" * 32)
            self.assertEqual((status, uploaded["status"], uploaded["pages_locked"]), (201, "ready", False))
            self.assertIn("页数", uploaded["note"])
            self.skipTest("Office converter stub not supplied")
        fixture = self.state / "fixture.pdf"
        fixture.write_bytes(make_pdf([(595, 842)] * 2))
        status, uploaded = self.client.upload("报告.docx", b"PK\x03\x04" + json.dumps({"pdf": str(fixture), "delay_ms": 200}).encode())
        self.assertEqual((status, uploaded["status"]), (201, "converting"))
        # Ordering waits for the real page count, quotes do not.
        self.assertEqual(self.client.json("/api/submit", self.client.order(uploaded))[0], 409)
        deadline = time.monotonic() + 30
        while True:
            status, record = self.client.json(f"/api/upload/{uploaded['upload_id']}")
            self.assertEqual(status, 200)
            if record["status"] != "converting":
                break
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.1)
        self.assertEqual((record["pages"], record["pages_locked"], record["preview"]), (2, True, "pdf"))
        self.assertEqual(self.client.raw(f"/api/preview/{uploaded['upload_id']}/2?w=480")[0], 200)
        _, broken = self.client.upload("坏表格.xlsx", b"PK\x03\x04" + json.dumps({"pdf": str(self.state / "missing.pdf")}).encode())
        deadline = time.monotonic() + 30
        while True:
            _, record = self.client.json(f"/api/upload/{broken['upload_id']}")
            if record["status"] != "converting":
                break
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.1)
        self.assertEqual((record["preview"], record["pages_locked"]), ("none", False))
        self.assertIn("转换失败", record["note"])

    def test_payment_codes(self):
        _, state = self.client.json("/api/state")
        self.assertEqual(state["features"]["payment_codes"], [])
        self.assertEqual(self.client.raw("/payment-code/wechat")[0], 404)
        codes = self.state / "payment-codes"
        codes.mkdir(exist_ok=True)
        (codes / "wechat.png").write_bytes(b"\x89PNG\r\n\x1a\nexample")
        try:
            self.assertEqual(self.client.json("/api/state")[1]["features"]["payment_codes"], ["wechat"])
            self.assertEqual(self.client.raw("/payment-code/wechat"), (200, b"\x89PNG\r\n\x1a\nexample"))
            self.assertEqual(self.client.raw("/payment-code/alipay")[0], 404)
            self.assertEqual(self.client.raw("/payment-code/../state.json")[0], 404)
        finally:
            (codes / "wechat.png").unlink()

    def test_parallel_students(self):
        def submit(index):
            student = Client(self.base)
            status, upload = student.upload(f"同学{index}.txt")
            self.assertEqual(status, 201)
            status, result = student.json("/api/submit", student.order(upload))
            self.assertEqual(status, 200)
            status, mine = student.json("/api/state")
            self.assertEqual(status, 200)
            self.assertEqual([row["job_id"] for row in mine["jobs"]], [result["job_id"]])
            return result["job_id"]
        with ThreadPoolExecutor(max_workers=8) as pool:
            identifiers = list(pool.map(submit, range(8)))
        self.assertEqual(len(set(identifiers)), 8)
        rows = self.app.backend.snapshot(fresh=True)["jobs"]
        self.assertTrue(set(identifiers).issubset({row["job_id"] for row in rows}))

    @unittest.skipUnless(importlib.util.find_spec("qrcode"), "optional QR dependency not installed")
    def test_qr_svg(self):
        status, svg = self.client.raw("/qr.svg")
        self.assertEqual(status, 200)
        self.assertEqual(ET.fromstring(svg).tag, "{http://www.w3.org/2000/svg}svg")

    def test_worker_execution(self):
        _, uploaded = self.client.upload("执行测试.txt")
        _, submitted = self.client.json("/api/submit", self.client.order(uploaded))
        self.app.backend.request("set_paused", paused=False)
        try:
            deadline = time.monotonic() + 15
            while True:
                self.app.backend.invalidate()
                _, snapshot = self.client.json("/api/state")
                row = next(row for row in snapshot["jobs"] if row["job_id"] == submitted["job_id"])
                if row["state"] == "completed":
                    break
                self.assertLess(time.monotonic(), deadline)
                time.sleep(.1)
            output = self.state / "output" / (submitted["job_id"] + ".txt")
            self.assertTrue(output.is_file())
            self.assertIn("physical_sheets=4", output.read_text(encoding="utf-8"))
            self.assertEqual(row["printed_sheets"], 6)
            self.assertEqual(row["pickup_code"], submitted["pickup_code"])
        finally:
            self.app.backend.request("set_paused", paused=True)
            self.app.backend.invalidate()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--coordinator", type=Path, required=True)
    parser.add_argument("--office-stub", type=Path)
    args, rest = parser.parse_known_args()
    EXECUTABLE = args.coordinator.resolve()
    OFFICE_STUB = args.office_stub.resolve() if args.office_stub else None
    unittest.main(argv=[__file__, *rest])
