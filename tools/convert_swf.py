#!/usr/bin/env python3
"""Convert an SWF trace to reproducible synthetic printing jobs (stdlib only)."""
import argparse
from collections import Counter
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, ROUND_CEILING, ROUND_HALF_UP
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

DEFAULT_URL = "https://raw.githubusercontent.com/wrench-project/task_clustering_batch_datasets/master/workloads/sdsc_sp2.swf"
MAX_TIME = 9007199254740991
MAX_BYTES = 128 * 1024 * 1024


def positive_decimal(text):
    try:
        value = Decimal(text)
    except InvalidOperation as error:
        raise argparse.ArgumentTypeError("expected a positive finite decimal") from error
    if not value.is_finite() or value <= 0:
        raise argparse.ArgumentTypeError("expected a positive finite decimal")
    return value


def convert(args):
    if not 1 <= args.count <= 10000 or args.offset < 0 or not 1 <= args.max_pages <= 1000:
        raise ValueError("count must be 1..10000, offset >= 0, max-pages 1..1000")
    if not 1 <= args.priority_levels <= 101:
        raise ValueError("priority-levels must be 1..101")
    if args.input and args.output.resolve() == args.input.resolve():
        raise ValueError("output must not replace input")
    if args.input:
        if args.input.stat().st_size > MAX_BYTES:
            raise ValueError("input exceeds 128 MiB")
        raw = args.input.read_bytes()
        origin = args.source_url or args.input.name
    else:
        with urllib.request.urlopen(args.url, timeout=60) as response:
            raw = response.read(MAX_BYTES + 1)
        origin = args.url
    if len(raw) > MAX_BYTES:
        raise ValueError("input exceeds 128 MiB")
    headers, rows, seen = [], [], set()
    skipped = Counter()
    for line_number, line in enumerate(raw.decode("utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if line.startswith(";"):
            headers.append(line)
            continue
        columns = line.split()
        if len(columns) != 18:
            skipped["not_18_columns"] += 1
            continue
        try:
            values = [Decimal(field) for field in columns]
            if any(not value.is_finite() for value in values):
                raise ValueError("non-finite value")
            job_id, submit, runtime, status = values[0], values[1], values[3], values[10]
            if job_id < 0 or job_id > MAX_TIME or job_id != job_id.to_integral_value() or submit < 0 or runtime <= 0:
                skipped["invalid_required_fields"] += 1
                continue
            if status != 1:
                skipped["not_completed_summary"] += 1
                continue
            original_id = str(int(job_id))
            if original_id in seen:
                skipped["duplicate_job_id"] += 1
                continue
            seen.add(original_id)
            rows.append((submit, runtime, original_id, columns[11], line_number))
        except (InvalidOperation, ValueError):
            skipped["invalid_number"] += 1
    rows.sort(key=lambda row: row[0])  # stable for simultaneous arrivals
    selected = rows[args.offset:args.offset + args.count]
    if len(selected) != args.count:
        raise ValueError(f"requested {args.count} jobs after offset {args.offset}, only {len(selected)} available")
    base = selected[0][0]
    jobs, clipped = [], 0
    for submit, runtime, original_id, user_id, line_number in selected:
        arrival = int(((submit - base) * 1000 * args.time_scale).to_integral_value(rounding=ROUND_HALF_UP))
        if not 0 <= arrival <= MAX_TIME:
            raise ValueError("arrival exceeds supported timeline; reduce time-scale")
        pages = int((runtime / args.seconds_per_page).to_integral_value(rounding=ROUND_CEILING))
        if pages > args.max_pages:
            clipped += 1
        priority = int(original_id) % args.priority_levels if args.priority_mode == "job-id-modulo" else 0
        jobs.append({"job_id": f"swf-{original_id}", "title": f"SWF job {original_id}",
                     "pages": min(args.max_pages, max(1, pages)), "copies": 1,
                     "submitted_ms": arrival, "priority": priority,
                     "source_job_id": original_id, "source_user_id": user_id,
                     "source_line": line_number, "source_submit_seconds": str(submit),
                     "source_runtime_seconds": str(runtime)})
    document = {"schema_version": 1, "kind": "campus-print-workload", "source": {
        "type": "synthetic-printing-mapped-from-swf", "url_or_name": origin,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "retrieved_at_utc": datetime.now(timezone.utc).isoformat() if not args.input else None,
        "converted_at_utc": datetime.now(timezone.utc).isoformat(),
        "headers": headers, "valid_records": len(rows), "skipped_records": dict(skipped),
        "mapping": {"offset": args.offset, "count": args.count, "time_scale": str(args.time_scale),
                    "arrival_rounding": "nearest millisecond, half up", "origin_submit_seconds": str(base),
                    "seconds_per_page": str(args.seconds_per_page), "page_rounding": "ceil then clip",
                    "max_pages": args.max_pages, "clipped_jobs": clipped,
                    "priority_mode": args.priority_mode, "priority_levels": args.priority_levels,
                    "selection": "status 1, nonnegative submit, positive runtime; stable sort by submit",
                    "randomness": "none", "original_wait_time": "ignored; recomputed by replay",
                    "processors": "ignored; printer count is a separate replay option"}}, "jobs": jobs}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Converted {len(jobs)} jobs; {clipped} page counts clipped; source SHA-256 {document['source']['sha256']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--input", type=Path, help="local SWF file; otherwise download --url")
    source.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--source-url", help="provenance URL for a local input")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--time-scale", type=positive_decimal, default=Decimal("0.01"))
    parser.add_argument("--seconds-per-page", type=positive_decimal, default=Decimal("60"))
    parser.add_argument("--max-pages", type=int, default=1000)
    parser.add_argument("--priority-mode", choices=["uniform", "job-id-modulo"], default="uniform")
    parser.add_argument("--priority-levels", type=int, default=5)
    args = parser.parse_args()
    try:
        convert(args)
    except (OSError, ValueError, ArithmeticError, UnicodeError) as error:
        print(f"convert_swf: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
