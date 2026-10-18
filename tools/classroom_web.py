#!/usr/bin/env python3
"""Classroom web student client and HTTP gateway to the local coordinator (Python 3.10+).

Phones get the full student client: upload with page preview, quotes for every
service tier, simulated payment, their own orders and the anonymised queue.
Run behind the supplied nginx configuration for an internet demonstration.
The coordinator remains the only writer of scheduler state. Uploaded documents
are stored privately in the state directory; workers still produce simulated
text output.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import hmac
import html
import io
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, unquote, urlsplit
import uuid
import webbrowser
import zlib

ROOT = Path(__file__).resolve().parents[1]
ASSETS = Path(__file__).resolve().parent / "classroom"
BRAND = ROOT / "assets" / "editorial"
MAX_FILE = 10 * 1024 * 1024
MAX_STORAGE = 300 * 1024 * 1024
MAX_CONVERTED = 64 * 1024 * 1024
MAX_PAGES = 1000
MAX_DOCS = 10
ID = re.compile(r"^[a-f0-9]{32}$")
IMAGES = {".png", ".jpg", ".jpeg"}
TEXTS = {".txt", ".md"}
OFFICE = {".doc", ".docx", ".ppt", ".pptx", ".xls", ".xlsx"}
EXTENSIONS = {".pdf", *IMAGES, *TEXTS, *OFFICE}
SERVICES = ("express", "standard", "flexible")
PREVIEW_WIDTHS = (480, 960, 1440)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
# Pages a browser may load without the classroom cookie: only the join form.
PUBLIC = {"/style.css": ("style.css", "text/css"), "/brand.png": (BRAND / "brand-128.png", "image/png"),
          "/favicon.png": (BRAND / "brand-64.png", "image/png")}
PRIVATE = {"/": ("index.html", "text/html"), "/present": ("present.html", "text/html"),
           "/app.js": ("app.js", "text/javascript"), "/present.js": ("present.js", "text/javascript")}
MACRO_SETTINGS = (b'<?xml version="1.0" encoding="UTF-8"?>'
                  b'<oor:items xmlns:oor="http://openoffice.org/2001/registry">'
                  b'<item oor:path="/org.openoffice.Office.Common/Security/Scripting">'
                  b'<prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop></item></oor:items>')


class Problem(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def job_id(upload_id):
    # Same UUID form as the Qt clients, so pickup codes keep six random characters.
    return str(uuid.UUID(hex=upload_id))


def pickup_code(identifier):
    # Must match cps::gui::text::pickupCode, which the owner console shows.
    return "".join(c for c in identifier if c.isalnum())[:6].upper()


def encode_png(width, height, stride, raw, channels):
    row = width * channels
    data = b"".join(b"\x00" + raw[y * stride:y * stride + row] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    header = struct.pack(">IIBBBBB", width, height, 8, {1: 0, 3: 2, 4: 6}[channels], 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(data, 6)) + chunk(b"IEND", b"")


def save_json(path, value):
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, ensure_ascii=False)
    os.replace(temporary, path)


def save_bytes(path, data):
    # Unique temporary name: two phones may render the same page at once.
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


class PdfEngine:
    """Serialised PDFium access (PDFium is not thread-safe); pinned copy from tools/setup_pdfium.py."""

    def __init__(self):
        self.lock = threading.Lock()
        try:
            import pypdfium2 as module
        except ImportError:
            module = None
            local = ROOT / ".tools" / "pdfium"
            if local.is_dir():
                sys.path.append(str(local))
                try:
                    import pypdfium2 as module
                except ImportError:
                    module = None
        self.module = module

    @property
    def available(self):
        return self.module is not None

    def open(self, data):
        try:
            return self.module.PdfDocument(data)
        except Exception as error:  # PdfiumError: damaged, encrypted or not a PDF
            raise Problem(422, "PDF 无法打开（可能已加密或损坏），请重新导出后再上传。") from error

    def pages(self, data):
        with self.lock:
            document = self.open(data)
            try:
                return len(document)
            finally:
                document.close()

    def render(self, data, index, width):
        with self.lock:
            document = self.open(data)
            try:
                page = document[index]
                try:
                    w, h = page.get_size()
                    scale = min(width / w, 4 * width / h) if w > 0 and h > 0 else 1
                    bitmap = page.render(scale=scale, rev_byteorder=True, fill_color=(255, 255, 255, 255))
                    try:
                        return encode_png(bitmap.width, bitmap.height, bitmap.stride, bytes(bitmap.buffer),
                                          bitmap.n_channels)
                    finally:
                        bitmap.close()
                finally:
                    page.close()
            finally:
                document.close()


def office_executable():
    # Prefer the bundled engine, then the private development installation.
    # An explicit override is authoritative.
    explicit = os.environ.get("CPS_OFFICE_EXECUTABLE")
    if explicit is not None:
        path = Path(explicit)
        return path if path.is_absolute() and path.is_file() else None
    candidates = []
    if sys.platform == "win32":
        roots = [ROOT / "office", ROOT / ".tools" / "libreoffice"] + [Path(os.environ[name]) / "LibreOffice" for name in
                                                     ("ProgramW6432", "ProgramFiles", "ProgramFiles(x86)") if os.environ.get(name)]
        candidates += [root / "program" / name for root in roots for name in ("soffice.com", "soffice.exe")]
        candidates += [shutil.which("soffice.com"), shutil.which("soffice.exe")]
    elif sys.platform == "darwin":
        candidates += [ROOT / "office/LibreOffice.app/Contents/MacOS/soffice",
                       ROOT / ".tools/libreoffice/LibreOffice.app/Contents/MacOS/soffice",
                       Path("/Applications/LibreOffice.app/Contents/MacOS/soffice")]
    candidates += [shutil.which("libreoffice"), shutil.which("soffice")]
    return next((Path(item) for item in candidates if item and Path(item).is_file()), None)


class OfficeConverter:
    """One background LibreOffice conversion at a time, with a private profile."""
    TIMEOUT = 90
    CAPACITY = 30

    def __init__(self, executable, finished):
        self.executable, self.finished = executable, finished
        self.condition = threading.Condition()
        self.pending = []
        if executable:
            threading.Thread(target=self.run, daemon=True).start()

    @property
    def available(self):
        return self.executable is not None

    def submit(self, identifier, source, extension, target):
        with self.condition:
            if len(self.pending) >= self.CAPACITY:
                return False
            self.pending.append((identifier, source, extension, target))
            self.condition.notify()
            return True

    def position(self, identifier):
        with self.condition:
            return next((index for index, item in enumerate(self.pending) if item[0] == identifier), -1)

    def run(self):
        while True:
            with self.condition:
                while not self.pending:
                    self.condition.wait()
                identifier, source, extension, target = self.pending[0]
            try:
                message = self.convert(source, extension, target)
            except OSError:
                message = "无法创建文档转换的临时目录。"
            with self.condition:
                self.pending.pop(0)
            self.finished(identifier, message)

    def convert(self, source, extension, target):
        filter_name = ("impress_pdf_Export" if extension.startswith(".ppt") else
                       "calc_pdf_Export" if extension.startswith(".xls") else "writer_pdf_Export")
        with tempfile.TemporaryDirectory(prefix="classroom-office-") as directory:
            root = Path(directory)
            # A private copy and profile per conversion; never reuse personal macros.
            document, output, profile = root / ("document" + extension), root / "output", root / "profile"
            (profile / "user").mkdir(parents=True)
            output.mkdir()
            shutil.copyfile(source, document)
            (profile / "user" / "registrymodifications.xcu").write_bytes(MACRO_SETTINGS)
            process = subprocess.Popen(
                [str(self.executable), "-env:UserInstallation=" + profile.as_uri(), "--headless", "--nologo",
                 "--nodefault", "--norestore", "--convert-to", "pdf:" + filter_name, "--outdir", str(output), str(document)],
                cwd=root, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=NO_WINDOW,
                start_new_session=os.name != "nt")
            try:
                code = process.wait(timeout=self.TIMEOUT)
            except subprocess.TimeoutExpired:
                self.kill(process)
                return "文档转换超时。"
            result = output / "document.pdf"
            if code or not result.is_file():
                return "文档转换失败，文件可能已损坏或受密码保护。"
            if result.stat().st_size > MAX_CONVERTED:
                return "转换后的 PDF 超过 64 MiB。"
            shutil.copyfile(result, target)
            return None

    @staticmethod
    def kill(process):
        # soffice.com starts soffice.bin; stop the whole tree, not only the launcher.
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, creationflags=NO_WINDOW)
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.kill()
        process.wait()


class Backend:
    def __init__(self, executable, state):
        self.executable, self.state = str(executable), str(state)
        # Reads may overlap; the coordinator serves several local clients at once.
        self.slots = threading.BoundedSemaphore(6)
        self.cache_lock = threading.Lock()
        self.mutation = threading.Lock()
        self.cached, self.cached_at = None, 0

    def request(self, kind, **payload):
        # The official CLI handles Unix sockets and Windows named pipes for us.
        with self.slots:
            try:
                result = subprocess.run(
                    [self.executable, "--state-dir", self.state, "--request",
                     json.dumps(dict(type=kind, **payload), ensure_ascii=False)],
                    capture_output=True, timeout=10, creationflags=NO_WINDOW)
                response = json.loads(result.stdout.decode("utf-8"))
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                raise Problem(503, "调度后台未响应。请稍后核对订单；系统没有自动重发操作。") from error
        if not isinstance(response, dict):
            raise Problem(503, "调度后台返回了无效响应。")
        if response.get("type") == "error":
            raise Problem(422, "后台拒绝操作：" + str(response.get("reason", "无效订单")))
        if result.returncode:
            raise Problem(503, "调度后台连接异常，请稍后核对订单。")
        return response

    def snapshot(self, fresh=False):
        # One shared snapshot per second, however many phones poll.
        with self.cache_lock:
            if fresh or self.cached is None or time.monotonic() - self.cached_at >= 1:
                self.cached = self.request("snapshot")
                self.cached_at = time.monotonic()
            return self.cached

    def invalidate(self):
        with self.cache_lock:
            self.cached = None


class Application:
    def __init__(self, coordinator, state, public_url, demo_code=None):
        self.state = Path(state).resolve()
        self.root = self.state / "classroom-web"
        self.uploads = self.root / "uploads"
        self.uploads.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        settings = self.root / "settings.json"
        try:
            # Exclusive creation preserves cookie identity across gateway restarts.
            with settings.open("x", encoding="utf-8") as output:
                os.chmod(settings, 0o600)
                json.dump(dict(secret=secrets.token_hex(32), code=secrets.token_hex(4).upper()), output)
        except FileExistsError:
            pass
        config = json.loads(settings.read_text(encoding="utf-8"))
        self.secret = bytes.fromhex(config["secret"])
        self.code = demo_code or config["code"]
        self.public_url = public_url.rstrip("/")
        parsed = urlsplit(self.public_url)
        if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.path not in {"", "/"} or parsed.query or parsed.fragment or parsed.username:
            raise ValueError("--public-url 必须是 http(s)://域名[:端口]，不含路径或查询参数")
        self.secure = parsed.scheme == "https"
        try:
            address = ipaddress.ip_address(parsed.hostname)
            self.access_hint = "此地址用于电脑本机自测；手机扫码演示请使用电脑的局域网地址。" if address.is_loopback else "手机与电脑连接同一 Wi-Fi 或热点后扫码。" if address.is_private else "手机可以使用 Wi-Fi 或流量扫码访问。"
        except ValueError:
            self.access_hint = "手机可以使用 Wi-Fi 或流量扫码访问。"
        self.join_url = self.public_url + "/join?code=" + quote(self.code, safe="")
        self.backend = Backend(coordinator, self.state)
        self.pool = ThreadPoolExecutor(max_workers=6)
        self.pdf = PdfEngine()
        # Office conversion is only useful when the converted PDF can be counted.
        self.office = OfficeConverter(office_executable() if self.pdf.available else None, self.converted)
        self.owners = {}
        for path in self.uploads.glob("*.json"):
            record = json.loads(path.read_text(encoding="utf-8"))
            if record.get("status") == "converting":
                # The gateway stopped mid-conversion; nothing will finish it now.
                record.update(status="ready", preview="none", note="文档转换被中断，请按实际页数填写。")
                save_json(path, record)
            self.owners.setdefault(record["customer"], set()).add(record["upload_id"])

    def signature(self, text):
        return hmac.new(self.secret, (self.code + ":" + text).encode(), hashlib.sha256).hexdigest()

    def session(self, cookie):
        try:
            parsed = SimpleCookie(cookie or "")
            value = parsed["classroom"].value
            identifier, signature = value.split(".", 1)
            if ID.fullmatch(identifier) and hmac.compare_digest(signature, self.signature(identifier)):
                return identifier
        except (KeyError, ValueError, TypeError):
            pass
        return None

    def cookie(self, identifier):
        return "classroom=" + identifier + "." + self.signature(identifier) + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400" + ("; Secure" if self.secure else "")

    def csrf(self, identifier):
        return self.signature("csrf:" + identifier)

    def owned(self, session):
        # Ownership comes from the signed browser session, never from Job::customer,
        # which only carries the optional display name typed by the student.
        owner = "web-" + session
        with self.lock:
            uploads = set(self.owners.get(owner, ()))
        return owner, {job_id(item) for item in uploads} | {"web-" + item for item in uploads}

    def metadata(self, identifier, session):
        if not isinstance(identifier, str) or not ID.fullmatch(identifier):
            raise Problem(404, "文件不存在。")
        try:
            with self.lock:
                record = json.loads((self.uploads / (identifier + ".json")).read_text(encoding="utf-8"))
        except FileNotFoundError as error:
            raise Problem(404, "文件不存在。") from error
        if record["customer"] != "web-" + session:
            raise Problem(404, "文件不存在。")
        return record

    def public(self, record):
        result = {key: value for key, value in record.items() if key not in {"customer", "sha256"}}
        if record.get("status") == "converting":
            result["queue_position"] = self.office.position(record["upload_id"])
        return result

    def upload(self, name, data, session):
        if not name or len(name.encode("utf-16-le")) > 400 or any(ord(c) < 32 for c in name) or "/" in name or "\\" in name:
            raise Problem(422, "文件名无效或超过 200 字符。")
        extension = Path(name).suffix.lower()
        if extension not in EXTENSIONS:
            raise Problem(422, "请选择 PDF、图片、文本或 Office 文档。")
        if not data:
            raise Problem(422, "不能上传空文件。")
        identifier = uuid.uuid4().hex
        record = dict(upload_id=identifier, customer="web-" + session, name=name, size=len(data),
                      sha256=hashlib.sha256(data).hexdigest(), kind="document", pages=1, pages_locked=False,
                      preview="none", status="ready", note="请按文档实际页数填写。")
        if extension == ".pdf":
            record["kind"] = "pdf"
            if self.pdf.available:
                pages = self.pdf.pages(data)
                if not 1 <= pages <= MAX_PAGES:
                    raise Problem(422, "PDF 须为 1–1000 页。")
                record.update(pages=pages, pages_locked=True, preview="pdf", note=f"已识别 {pages} 页，页数随文档确定。")
        elif extension in IMAGES:
            record.update(kind="image", pages_locked=True, preview="image", note="图片按 1 页打印。")
        elif extension in TEXTS:
            record.update(kind="text", preview="text", pages=min(MAX_PAGES, max(1, math.ceil(len(data.splitlines()) / 50))),
                          note="文本按每页约 50 行估算，请确认。")
        else:
            record["kind"] = "office"
            ooxml, compound = data.startswith(b"PK\x03\x04"), data.startswith(bytes.fromhex("d0cf11e0a1b11ae1"))
            if not compound and not (extension.endswith("x") and ooxml) and not (extension == ".doc" and data.startswith(b"{\\rtf")):
                raise Problem(422, "文档格式与扩展名不符，文件可能已损坏；请重新保存或导出 PDF。")
            if self.office.available:
                record.update(status="converting", preview="pdf", note="正在转换文档，完成后自动显示预览和页数。")
            else:
                record["note"] = "展示电脑未安装 Office 预览组件，请按实际页数填写，或导出 PDF 后上传。"
        with self.lock:
            # The quota includes unfinished uploads and survives restarts.
            records = list(self.uploads.glob("*.json"))
            total = sum(path.stat().st_size for path in self.uploads.glob("*.bin"))
            own = len(self.owners.get(record["customer"], ()))
            if len(records) >= 300 or total + len(data) > MAX_STORAGE or own >= 10:
                raise Problem(429, "本次课堂演示的上传额度已用完，请联系展示者。")
            path = self.uploads / (identifier + ".bin")
            try:
                with path.open("xb") as output:
                    output.write(data)
                save_json(self.uploads / (identifier + ".json"), record)
            except OSError:
                path.unlink(missing_ok=True)
                raise
            self.owners.setdefault(record["customer"], set()).add(identifier)
        if record["status"] == "converting":
            if not self.office.submit(identifier, path, extension, self.uploads / (identifier + ".pdf")):
                self.converted(identifier, "转换排队已满。")
                record = self.metadata(identifier, session)
        return self.public(record)

    def converted(self, identifier, problem):
        target = self.uploads / (identifier + ".pdf")
        pages = 0
        if problem is None:
            try:
                pages = self.pdf.pages(target.read_bytes())
                if not 1 <= pages <= MAX_PAGES:
                    problem = "转换后的文档须为 1–1000 页。"
            except (Problem, OSError):
                problem = "转换后的文档无法预览。"
        with self.lock:
            path = self.uploads / (identifier + ".json")
            record = json.loads(path.read_text(encoding="utf-8"))
            if problem is None:
                record.update(status="ready", pages=pages, pages_locked=True, preview="pdf",
                              note=f"已转换并识别 {pages} 页，页数随文档确定。")
            else:
                record.update(status="ready", preview="none", note=problem + " 请按实际页数填写，或导出 PDF 后上传。")
            save_json(path, record)

    def preview(self, identifier, page, width, session):
        record = self.metadata(identifier, session)
        if record.get("preview") != "pdf" or record.get("status") != "ready" or not 1 <= page <= record["pages"]:
            raise Problem(404, "没有可预览的页面。")
        cache = self.uploads / (identifier + ".preview") / f"{page}-{width}.png"
        try:
            return cache.read_bytes()
        except FileNotFoundError:
            pass
        source = self.uploads / (identifier + (".bin" if record["kind"] == "pdf" else ".pdf"))
        image = self.pdf.render(source.read_bytes(), page - 1, width)
        cache.parent.mkdir(exist_ok=True)
        save_bytes(cache, image)
        return image

    def resolve_documents(self, documents, session, preview):
        # A merged multi-file order carries one entry per uploaded file; the first
        # file anchors the job id, ownership and title, so the coordinator schema and
        # the single-file path stay untouched.
        if len(documents) > MAX_DOCS:
            raise Problem(422, f"一次最多合并 {MAX_DOCS} 个文件。")
        records, seen, total = [], set(), 0
        for item in documents:
            identifier = item.get("upload_id") if isinstance(item, dict) else None
            pages = item.get("pages") if isinstance(item, dict) else None
            if not isinstance(identifier, str) or not ID.fullmatch(identifier):
                raise Problem(404, "文件不存在。")
            if identifier in seen:
                raise Problem(422, "同一文件不能重复提交。")
            if type(pages) is not int or not 1 <= pages <= MAX_PAGES:
                raise Problem(422, "每个文件的页数须为 1–1000 的整数。")
            seen.add(identifier)
            record = self.metadata(identifier, session)
            if not preview and record.get("status") == "converting":
                raise Problem(409, "文档仍在转换，完成后会自动显示页数。")
            if record.get("pages_locked") and pages != record["pages"]:
                raise Problem(422, "页数已按文档自动识别，不能修改。")
            records.append(record)
            total += pages
        if not 1 <= total <= MAX_PAGES:
            raise Problem(422, "合计页数须为 1–1000。")
        return records, total

    def order(self, body, session, preview=False):
        documents = body.get("documents")
        record = None
        if isinstance(documents, list) and documents:
            records, total = self.resolve_documents(documents, session, preview)
            suffix = f" 等{len(records)}份" if len(records) > 1 else ""
            title = (records[0]["name"] + suffix)[:200]
            anchor = records[0]["upload_id"]
            body = {**body, "pages": total}
        else:
            # A quote may come before any file is chosen, as in the Qt student client.
            record = None if preview and body.get("upload_id") is None else self.metadata(body.get("upload_id"), session)
            if not preview and record.get("status") == "converting":
                raise Problem(409, "文档仍在转换，完成后会自动显示页数。")
            title = record["name"] if record else "报价预览"
            anchor = record["upload_id"] if record else None
        for field, minimum, maximum in (("pages", 1, MAX_PAGES), ("copies", 1, 100)):
            if type(body.get(field)) is not int or not minimum <= body[field] <= maximum:
                raise Problem(422, "页数须为 1–1000，份数须为 1–100 的整数。")
        if record and record.get("pages_locked") and body["pages"] != record["pages"]:
            raise Problem(422, "页数已按文档自动识别，不能修改。")
        if body["pages"] * body["copies"] > 10000:
            raise Problem(422, "总面数（页数 × 份数）不能超过 10000。")
        for field in ("color", "duplex"):
            if type(body.get(field)) is not bool:
                raise Problem(422, "打印选项无效。")
        for field, choices in (("paper", {"a4", "a3"}), ("service", set(SERVICES)),
                               ("payment", {"wechat", "alipay", "campus_card"})):
            if not isinstance(body.get(field), str) or body[field] not in choices:
                raise Problem(422, "打印选项无效。")
        student = body.get("student", "")
        if not isinstance(student, str) or len(student.strip()) > 32 or any(ord(c) < 32 for c in student):
            raise Problem(422, "学号或昵称不能超过 32 个字符。")
        # Price, priority, ownership and job id can never be supplied by a phone.
        return dict(job_id=job_id(anchor) if anchor else "quote-preview",
                    title=title,
                    customer=student.strip() or "访客 " + session[:4].upper(),
                    **{key: body[key] for key in ("pages", "copies", "paper", "color", "duplex", "service", "payment")})

    def quote(self, body, session):
        job = self.order(body, session, preview=True)
        # A quote never enqueues anything, so a fixed preview id is safe to reuse.
        jobs = [{**job, "job_id": "quote-preview", "service": service} for service in SERVICES]
        replies = dict(zip(SERVICES, self.pool.map(lambda item: self.backend.request("quote", job=item), jobs)))
        result = dict(replies[job["service"]])
        result["tiers"] = {service: reply.get("quoted_cents") for service, reply in replies.items()}
        return result

    def submit(self, body, session):
        job = self.order(body, session)
        documents = body.get("documents")
        anchor = documents[0]["upload_id"] if isinstance(documents, list) and documents else body["upload_id"]
        legacy = "web-" + anchor
        # Stable per-upload id makes a lost HTTP reply safe to reconcile, including
        # after a gateway restart. Never retry a timed-out mutation internally.
        with self.backend.mutation:
            rows = self.backend.snapshot(fresh=True)["jobs"]
            existing = next((row for row in rows if row["job_id"] in {job["job_id"], legacy}), None)
            if existing:
                if existing["job_id"] == legacy or any(existing.get(key) != value for key, value in job.items()):
                    raise Problem(409, "这个文件已提交过订单，请查看我的订单；更改规格需要重新选择文件。")
            else:
                self.backend.request("submit", job=job)
            self.backend.invalidate()
        # The accepted order remains successful if optional cover persistence fails.
        # These local files are not exposed by HTTP and never enter coordinator IPC.
        try:
            self.save_order_cover(anchor, job["job_id"], session)
        except Exception:
            print("Order accepted; optional merchant cover could not be saved.", file=sys.stderr)
        return dict(job_id=job["job_id"], pickup_code=pickup_code(job["job_id"]), message="订单已进入队列，支付为模拟。")

    def save_order_cover(self, identifier, order_id, session):
        record = self.metadata(identifier, session)
        directory = self.state / "order-previews"
        if any((directory / (order_id + suffix)).is_file() for suffix in (".png", ".jpg", ".txt")):
            return
        if record.get("preview") == "pdf":
            image = self.preview(identifier, 1, 960, session)
            suffix, data = ".png", image
        elif record.get("kind") == "image":
            suffix = ".png" if record["name"].lower().endswith(".png") else ".jpg"
            data = (self.uploads / (identifier + ".bin")).read_bytes()
        elif record.get("kind") == "text":
            # Keep a bounded first-page text excerpt; the Qt owner renders it with
            # its installed CJK fonts, without adding a second Office/PDF engine.
            with (self.uploads / (identifier + ".bin")).open("rb") as source:
                text = source.read(64 * 1024).decode("utf-8", errors="replace")
            suffix, data = ".txt", "\n".join(text.splitlines()[:50])[:4096].encode("utf-8")
        else:
            return
        directory.mkdir(exist_ok=True)
        save_bytes(directory / (order_id + suffix), data)

    def cancel(self, body, session):
        identifier = body.get("job_id")
        _, own = self.owned(session)
        row = next((row for row in self.backend.snapshot(fresh=True)["jobs"] if row["job_id"] == identifier), None)
        if row is None or (identifier not in own and row.get("customer") != "web-" + session):
            raise Problem(404, "订单不存在。")
        with self.backend.mutation:
            reply = self.backend.request("cancel", job_id=identifier)
            self.backend.invalidate()
        return reply

    def payment_code(self, method):
        for extension in (".png", ".jpg", ".jpeg"):
            path = self.state / "payment-codes" / (method + extension)
            if path.is_file():
                return path
        return None

    def state_for(self, session):
        snap = self.backend.snapshot()
        owner, own_ids = self.owned(session)
        rows = snap["jobs"]

        def mine(row):
            return row["job_id"] in own_ids or row.get("customer") == owner

        def known(row):
            return isinstance(row.get("estimated_finish_ms"), (int, float))
        jobs = []
        for row in rows:
            if not mine(row):
                continue
            fields = ("job_id", "title", "pages", "copies", "state", "printed_sheets", "quoted_cents", "refund_cents",
                      "payment", "slot", "jobs_ahead", "estimated_start_ms", "estimated_finish_ms", "estimate_blocked",
                      "deadline_ms", "submitted_ms", "paper", "color", "duplex", "service", "recovering")
            view = {key: row[key] for key in fields if key in row}
            view.update(sheets=row["pages"] * row["copies"], pickup_code=pickup_code(row["job_id"]))
            jobs.append(view)
        jobs.sort(key=lambda row: row.get("submitted_ms") or 0, reverse=True)
        # Forecast order first (as the Qt student client lists it); unknown estimates last.
        order = {identifier: index for index, identifier in enumerate(snap.get("waiting_order", []))}
        waiting = sorted((row for row in rows if row["state"] == "queued"), key=lambda row: order.get(row["job_id"], len(order)))
        waiting.sort(key=lambda row: (0, row.get("jobs_ahead", 0)) if known(row) else (1, 0))
        queue = []
        for row in waiting[:50]:
            own = mine(row)
            # Other students' titles and names never leave the gateway.
            queue.append(dict(own=own, title=row["title"] if own else None, paper=row.get("paper"), color=row.get("color"),
                              duplex=row.get("duplex"), sheets=row["pages"] * row["copies"],
                              estimated_start_ms=row.get("estimated_start_ms"), estimated_finish_ms=row.get("estimated_finish_ms"),
                              estimate_blocked=row.get("estimate_blocked", ""), jobs_ahead=row.get("jobs_ahead", 0)))
        active = {row["slot"]: row for row in rows if row.get("slot", -1) >= 0 and row["state"] in {"running", "cancelling"}}
        devices = []
        for index, printer in enumerate(snap.get("fleet", [])):
            row = active.get(index)
            devices.append(dict(name=printer["name"], a3=printer.get("a3", False), color=printer.get("color", False),
                                duplex=printer.get("duplex", False), online=printer.get("online", True), busy=row is not None,
                                state=row["state"] if row else None, recovering=bool(row and row.get("recovering")),
                                mine=bool(row and mine(row)), printed=row.get("printed_sheets", 0) if row else 0,
                                total=row["pages"] * row["copies"] if row else 0,
                                estimated_finish_ms=row.get("estimated_finish_ms") if row else None))
        counts = {state: sum(row["state"] == state for row in rows) for state in ("queued", "running", "completed")}
        codes = [method for method in ("wechat", "alipay") if self.payment_code(method)]
        return dict(jobs=jobs, queue=queue, queue_total=len(waiting), devices=devices, counts=counts,
                    policy=snap["policy"], paused=snap["paused"], shop_enabled=snap.get("shop_enabled", False),
                    now_ms=snap["now_ms"], forecast_at_ms=snap.get("forecast_at_ms", snap["now_ms"]),
                    queue_clear_ms=snap.get("queue_clear_ms"), visitor="访客 " + session[:4].upper(),
                    features=dict(pdf_preview=self.pdf.available, office_preview=self.office.available, payment_codes=codes),
                    csrf=self.csrf(session), join_url=self.join_url, access_hint=self.access_hint)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, application):
        self.application = application
        self.slots = threading.BoundedSemaphore(32)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            try:
                request.sendall(b"HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    server_version = "ClassroomPrint/2"

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, format_string, *args):
        # Keep join codes, cookies and document names out of access logs.
        if self.command == "GET" and len(args) > 1 and str(args[1]).startswith(("2", "3")):
            return
        print(self.command, urlsplit(self.path).path, args[1] if len(args) > 1 else "", flush=True)

    def reply(self, status, data, content_type="application/json; charset=utf-8", headers=None):
        if not isinstance(data, bytes):
            data = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        for name, value in {"Content-Type": content_type, "Content-Length": str(len(data)), "Cache-Control": "no-store",
                            "X-Content-Type-Options": "nosniff", "Referrer-Policy": "no-referrer",
                            "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob: data:; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'",
                            **(headers or {})}.items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(data)

    def asset(self, entry):
        name, mime = entry
        path = name if isinstance(name, Path) else ASSETS / name
        self.reply(200, path.read_bytes(), mime + ("; charset=utf-8" if mime.startswith("text/") else ""))

    def join_page(self, status, message=""):
        page = (ASSETS / "join.html").read_text(encoding="utf-8").replace("<!--ERROR-->", html.escape(message))
        self.reply(status, page.encode("utf-8"), "text/html; charset=utf-8")

    def execute(self, method):
        try:
            self.route(method)
        except Problem as error:
            self.reply(error.status, dict(error=error.message))
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        except OSError:
            self.reply(503, dict(error="文件存储暂不可用，请联系展示者。"))
        except Exception:
            self.reply(500, dict(error="服务暂不可用，请联系展示者并核对订单。"))

    def do_GET(self):
        self.execute("GET")

    def do_POST(self):
        self.execute("POST")

    def route(self, method):
        app = self.server.application
        url = urlsplit(self.path)
        path, query = url.path, parse_qs(url.query)
        session = app.session(self.headers.get("Cookie"))
        if method == "GET" and path == "/join":
            if "code" not in query:
                self.join_page(200)
                return
            code = query["code"][0].strip()
            if not hmac.compare_digest(code.upper().encode(), app.code.upper().encode()):
                self.join_page(403, "课堂码无效，请核对展示者提供的二维码或课堂码。")
                return
            session = session or uuid.uuid4().hex
            destination = "/present" if query.get("view") == ["present"] else "/"
            self.reply(303, b"", headers={"Location": destination, "Set-Cookie": app.cookie(session)})
            return
        if method == "GET" and path == "/health":
            app.backend.snapshot()
            self.reply(200, dict(ok=True))
            return
        if method == "GET" and path in PUBLIC:
            self.asset(PUBLIC[path])
            return
        if not session:
            if method == "GET" and path in {"/", "/present"}:
                self.join_page(200)
                return
            raise Problem(401, "请扫描课堂二维码进入。")
        if method == "GET" and path in PRIVATE:
            self.asset(PRIVATE[path])
            return
        if method == "GET" and path == "/qr.svg":
            try:
                import qrcode
                from qrcode.image.svg import SvgPathImage
            except ImportError as error:
                raise Problem(503, "缺少二维码依赖，请安装 tools/classroom/requirements.txt。") from error
            output = io.BytesIO()
            qrcode.make(app.join_url, image_factory=SvgPathImage, box_size=10, border=4).save(output)
            self.reply(200, output.getvalue(), "image/svg+xml")
            return
        if method == "GET" and path == "/api/state":
            self.reply(200, app.state_for(session))
            return
        match = re.fullmatch(r"/api/upload/([a-f0-9]{32})", path)
        if method == "GET" and match:
            self.reply(200, app.public(app.metadata(match[1], session)))
            return
        match = re.fullmatch(r"/api/preview/([a-f0-9]{32})/([0-9]{1,4})", path)
        if method == "GET" and match:
            width = int(query.get("w", ["960"])[0]) if query.get("w", ["960"])[0].isdigit() else 0
            if width not in PREVIEW_WIDTHS:
                raise Problem(404, "没有可预览的页面。")
            image = app.preview(match[1], int(match[2]), width, session)
            # Pages of an upload never change; let the phone cache them.
            self.reply(200, image, "image/png", {"Cache-Control": "private, max-age=86400, immutable"})
            return
        match = re.fullmatch(r"/api/document/([a-f0-9]{32})", path)
        if method == "GET" and match:
            record = app.metadata(match[1], session)
            if record.get("kind") not in {"image", "text"}:
                raise Problem(404, "该文件没有可直接预览的内容。")
            data = (app.uploads / (match[1] + ".bin")).read_bytes()
            mime = ("image/png" if record["name"].lower().endswith(".png") else "image/jpeg") \
                if record["kind"] == "image" else "text/plain; charset=utf-8"
            self.reply(200, data, mime, {"Cache-Control": "private, max-age=86400, immutable"})
            return
        match = re.fullmatch(r"/payment-code/(wechat|alipay)", path)
        if method == "GET" and match:
            code = app.payment_code(match[1])
            if not code:
                raise Problem(404, "未配置收款码。")
            self.reply(200, code.read_bytes(), "image/png" if code.suffix == ".png" else "image/jpeg")
            return
        if method != "POST" or path not in {"/api/upload", "/api/quote", "/api/submit", "/api/cancel"}:
            raise Problem(404, "页面不存在。")
        if not hmac.compare_digest(self.headers.get("X-CSRF-Token", "").encode(), app.csrf(session).encode()):
            raise Problem(403, "页面已过期，请刷新后重试。")
        # Browser fetch sets Content-Length for File/JSON bodies. Reject unbounded
        # or ambiguous requests; nginx buffers uploads before forwarding them.
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get("Transfer-Encoding") or len(lengths) != 1 or not lengths[0].isdigit():
            raise Problem(411, "需要明确的文件大小。")
        length = int(lengths[0])
        limit = MAX_FILE if path == "/api/upload" else 8192
        if length <= 0 or length > limit:
            raise Problem(413, "文件不能为空且不能超过 10 MiB；订单参数不能超过 8 KiB。")
        data = self.rfile.read(length)
        if len(data) != length:
            raise Problem(400, "上传未完成，请重新选择文件。")
        if path == "/api/upload":
            self.reply(201, app.upload(unquote(self.headers.get("X-File-Name", "")), data, session))
            return
        try:
            body = json.loads(data)
        except (ValueError, UnicodeError) as error:
            raise Problem(400, "订单参数格式无效。") from error
        if not isinstance(body, dict):
            raise Problem(400, "订单参数格式无效。")
        handler = {"/api/quote": app.quote, "/api/submit": app.submit, "/api/cancel": app.cancel}[path]
        self.reply(200, handler(body, session))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coordinator", required=True, type=Path)
    parser.add_argument("--state-dir", required=True, type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8787, type=int)
    parser.add_argument("--public-url", help="二维码入口，例如 https://print.example.com")
    parser.add_argument("--demo-code", help="课堂码；默认生成并保存在本次数据目录")
    parser.add_argument("--open-browser", action="store_true", help="启动后打开二维码展示页")
    args = parser.parse_args()
    if not args.coordinator.is_file():
        parser.error("找不到 print-coordinator，请先构建项目。")
    app = Application(args.coordinator.resolve(), args.state_dir, args.public_url or f"http://127.0.0.1:{args.port}", args.demo_code)
    deadline = time.monotonic() + 15
    while True:
        try:
            if not app.backend.snapshot(fresh=True).get("shop_enabled"):
                parser.error("需要经营模式后台，请用 --shop-mode 或老板窗口启动。")
            break
        except Problem as error:
            if time.monotonic() >= deadline:
                parser.error(error.message + " 请先启动同一 --data-dir 的老板窗口。")
            time.sleep(.3)
    server = Server((args.host, args.port), app)
    print("扫码入口：" + app.join_url, flush=True)
    print("课堂码：" + app.code + "（也可打开 " + app.public_url + " 手动输入）", flush=True)
    print("展示页：先打开扫码入口，再访问 " + app.public_url + "/present", flush=True)
    print("上传文件保存在：" + str(app.uploads) + "（模拟打印）", flush=True)
    print("PDF 预览：" + ("已启用" if app.pdf.available else "未启用，请运行 python tools/setup_pdfium.py") +
          " · Office 预览：" + ("已启用" if app.office.available else "未启用，学生需手动填写页数"), flush=True)
    if args.open_browser:
        webbrowser.open(app.join_url + "&view=present")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
