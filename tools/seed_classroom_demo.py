"""Submit synthetic files through the real classroom HTTP flow as separate visitors."""
import argparse
import json
import struct
import zlib
from http.cookiejar import CookieJar
from pathlib import Path
from urllib.parse import quote
from urllib.request import Request, build_opener, HTTPCookieProcessor, ProxyHandler


def pdf(pages, number):
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>",
               ("<< /Type /Pages /Kids [%s] /Count %d >>" %
                (" ".join(f"{4 + i * 2} 0 R" for i in range(pages)), pages)).encode(),
               b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    for index in range(pages):
        content = f"BT /F1 16 Tf 50 780 Td (Classroom mock document {number} - page {index + 1}) Tj ET".encode()
        objects += [(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents {5 + index * 2} 0 R >>").encode(),
                    f"<< /Length {len(content)} >>\nstream\n".encode() + content + b"\nendstream"]
    data, offsets = b"%PDF-1.4\n", []
    for index, body in enumerate(objects, 1):
        offsets.append(len(data))
        data += f"{index} 0 obj\n".encode() + body + b"\nendobj\n"
    table = len(data)
    data += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
    data += b"".join(f"{offset:010d} 00000 n \n".encode() for offset in offsets)
    return data + f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{table}\n%%EOF\n".encode()


def png(number):
    def chunk(kind, content):
        return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content) & 0xffffffff)
    pixel = bytes((40 + number % 100, 140, 185))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 32, 32, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress((b"\0" + pixel * 32) * 32)) + chunk(b"IEND", b""))


def seed(url, code, output, count=12):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    receipts = []
    for index in range(count):
        opener = build_opener(ProxyHandler({}), HTTPCookieProcessor(CookieJar()))
        def request(path, data=None, headers=None):
            with opener.open(Request(url.rstrip("/") + path, data, headers or {}), timeout=30) as response:
                return response.read()
        request("/join?code=" + quote(code))
        csrf = json.loads(request("/api/state"))["csrf"]
        pages = (4, 18, 36, 12)[index % 4]
        if index % 3 == 0:
            name, content = f"模拟讲义-{index + 1:02d}.pdf", pdf(pages, index + 1)
        elif index % 3 == 1:
            name, content = f"模拟报告-{index + 1:02d}.txt", ("课堂模拟文件，仅供演示。\n" * (pages * 50)).encode()
        else:
            name, content = f"模拟海报-{index + 1:02d}.png", png(index)
        (output / name).write_bytes(content)
        uploaded = json.loads(request("/api/upload", content, {"X-CSRF-Token": csrf, "X-File-Name": quote(name), "Content-Type": "application/octet-stream"}))
        order = dict(upload_id=uploaded["upload_id"], pages=uploaded["pages"], copies=(2, 4, 20)[index % 3],
                     paper="a3" if index % 4 == 0 else "a4", color=index % 3 == 2,
                     duplex=index % 3 != 2, service=("express", "standard", "flexible")[index % 3],
                     student=f"模拟同学{index + 1:02d}", payment="campus_card")
        headers = {"Content-Type": "application/json", "X-CSRF-Token": csrf}
        quoted = json.loads(request("/api/quote", json.dumps(order).encode(), headers))
        accepted = json.loads(request("/api/submit", json.dumps(order).encode(), headers))
        # The mobile state must immediately contain this visitor's accepted order.
        state = json.loads(request("/api/state"))
        own = next(row for row in state["jobs"] if row["job_id"] == accepted["job_id"])
        assert own["title"] == name and own["pages"] == uploaded["pages"]
        assert own["quoted_cents"] == quoted["quoted_cents"]
        assert all(row["job_id"] == accepted["job_id"] for row in state["jobs"]), "visitor received another visitor's order"
        receipts.append(dict(file=name, job_id=accepted["job_id"], pages=own["pages"], quote_cents=own["quoted_cents"],
                             status=own["state"], pickup_code=accepted["pickup_code"], feedback_verified=True))
        print(f"{index + 1}/{count} accepted: {name} ({own['pages']} pages)", flush=True)
    (output / "receipts.json").write_text(json.dumps(receipts, ensure_ascii=False, indent=2), encoding="utf-8")
    return receipts


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--code", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--count", type=int, default=12, choices=range(1, 41))
    args = parser.parse_args()
    seed(args.url, args.code, args.output, args.count)
