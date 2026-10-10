"""Check economic comparisons against the saved input and dispatch evidence."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

AUDIT_PATH = Path(__file__).resolve().parents[1] / 'tools/run_scheduling_benchmark.py'
SPEC = importlib.util.spec_from_file_location('scheduling_benchmark', AUDIT_PATH)
BENCHMARK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCHMARK)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--experiment", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="经营 实验-") as root:
        outputs = [Path(root) / f"result-{i}.json" for i in range(2)]
        for output in outputs:
            subprocess.run([args.experiment, "--output", str(output), "--customers", "60", "--hours", "1"],
                           check=True, capture_output=True, timeout=20)
        a, b = (json.loads(p.read_text(encoding="utf-8")) for p in outputs)
        assert a == b, "fixed seed must reproduce every input and dispatch"
        assert len(a["runs"]) == 21
        assert {r["policy"] for r in a["runs"]} == {"fcfs", "sjf", "balanced"}
        BENCHMARK.audit_report(a)
        cohorts = {}
        for run in a["runs"]:
            jobs = {j["job_id"]: j for j in run["jobs"]}
            assert run["accepted"] + run["lost_before_order"] == 60
            completed = [j for j in jobs.values() if j["state"] == "completed"]
            assert run["completed"] == len(completed)
            assert all(j["finished_ms"] <= a["horizon_ms"] for j in completed)
            assert run["revenue_cents"] == sum(j["quoted_cents"] - j["refund_cents"] for j in completed)
            assert run["cost_cents"] == sum(j["cost_cents"] for j in completed)
            assert run["contribution_cents"] == run["revenue_cents"] - run["cost_cents"]
            assert run["late"] == sum(j["finished_ms"] > j["deadline_ms"] for j in completed)
            assert run["unfinished_at_close"] == len(jobs) - len(completed)
            first_waits = [j["first_started_ms"] - j["submitted_ms"] for j in jobs.values()
                           if j["first_started_ms"] is not None]
            assert run["first_started"] == len(first_waits)
            assert run["max_first_wait_ms"] == max(first_waits, default=0)
            key = (run["mode"], run["budget_sensitive_percent"], run["discounts"])
            inputs = [(j["job_id"], j["submitted_ms"], j["pages"], j["copies"], j["quoted_cents"], j["deadline_ms"])
                      for j in jobs.values()]
            if key in cohorts:
                assert cohorts[key] == inputs, "policy comparisons must use the same accepted orders and prices"
            cohorts[key] = inputs
            ends = [0] * len(a["fleet"])
            for d in run["dispatches"]:
                j, p = jobs[d["job_id"]], a["fleet"][d["printer"]]
                assert d["started_ms"] >= max(j["submitted_ms"], ends[d["printer"]])
                assert not j["color"] or p["color"]
                assert j["paper"] != "a3" or p["a3"]
                assert not j["duplex"] or p["duplex"]
                per_page = p["color_page_ms"] if j["color"] else p["mono_page_ms"]
                assert d["finished_ms"] - d["started_ms"] == per_page * j["pages"] * j["copies"] * (2 if j["paper"] == "a3" else 1)
                ends[d["printer"]] = d["finished_ms"]
        result = BENCHMARK.run_benchmark(args.experiment, Path(root)/'held-out')
        assert result['passed'], f"balanced must beat both baselines on held-out mean quality: {result['policy_means']}"
        print("Three strategies, independent quality audit and 84 held-out cohorts passed:", result['policy_means'])


if __name__ == "__main__":
    main()
