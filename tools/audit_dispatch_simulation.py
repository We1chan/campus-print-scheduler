"""Independently audit an exported scheduling-workspace business simulation."""
import argparse
import json
import hashlib
from pathlib import Path

from run_scheduling_benchmark import audit_run, require


def audit(path):
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    require(report["kind"] == "campus-business-simulation" and report["clock_scale"] == 1,
            "expected business simulation using unscaled milliseconds")
    require(report["evaluation"] == "balanced-shop-quality-v1" and
            report["score_weights"] == [1, 1, 1] and report["wait_reference_ms"] == 1800000,
            "evaluation must retain the fixed production score")
    if report["generator"] == "campus-profiles-v1":
        require(len(report["fleet"]) == 6 and report["horizon_ms"] == 28800000, "six devices and eight-hour window required")
    else:
        require(report["generator"] == "classroom-snapshot-v1" and 1 <= len(report["fleet"]) <= 6 and report["horizon_ms"] >= 28800000,
                "classroom replay must preserve its actual fleet and observation window")
        source = report["source_snapshot"]
        eligible = {j["job_id"]: j for j in source["jobs"] if j["state"] not in ("cancelled", "failed", "cancelling") and not j.get("refund_cents", 0)}
        require({j["job_id"] for j in report["inputs"]} == set(eligible), "classroom input cohort differs from backend snapshot")
        origin = min([source["now_ms"]] + [j["submitted_ms"] for j in eligible.values()])
        require(report["origin_ms"] == origin and report["fleet"] == source["fleet"] and
                report["supplied_a4"] == source["supplied_a4"] and report["supplied_a3"] == source["supplied_a3"],
                "classroom replay changed source clock, fleet or paper supply")
        for j in report["inputs"]:
            original = eligible[j["job_id"]]
            require(all(j[k] == original[k] for k in ("title", "pages", "copies", "paper", "color", "duplex", "service", "base_cents", "quoted_cents")),
                    "classroom replay changed locked document terms")
            require(j["submitted_ms"] == original["submitted_ms"] - origin and
                    j["deadline_ms"] == (original["deadline_ms"] - origin if original["deadline_ms"] else 0),
                    "classroom replay changed relative arrival or pickup deadline")
    require([r["policy"] for r in report["runs"]] == ["fcfs", "sjf", "balanced"], "three matched strategies required")
    canonical = json.dumps(report["inputs"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    require(hashlib.sha256(canonical).hexdigest() == report["input_sha256"], "input fingerprint mismatch")
    fields = ("job_id", "title", "pages", "copies", "submitted_ms", "paper", "color", "duplex",
              "service", "base_cents", "quoted_cents", "deadline_ms")
    inputs = {j["job_id"]: tuple(j[f] for f in fields) for j in report["inputs"]}
    require(len(inputs) == len(report["inputs"]), "duplicate inputs")
    metrics = {}
    for run in report["runs"]:
        require({j["job_id"]: tuple(j[f] for f in fields) for j in run["jobs"]} == inputs,
                "a strategy changed the common input cohort")
        metrics[run["policy"]] = audit_run(report, run)
    return metrics


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.report), ensure_ascii=False, indent=2))
