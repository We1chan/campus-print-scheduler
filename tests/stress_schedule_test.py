"""Independent checks for reproducible overload generation and baseline rules."""
import copy
from fractions import Fraction
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_stress_schedule as stress
from solve_optimal_schedule import evaluate


def order(ident, *, pages=1, copies=1, release=0, paper="a4", color=False,
          duplex=False, service="standard"):
    return stress.quote_job({"job_id": ident, "title": ident, "pages": pages,
                             "copies": copies, "submitted_ms": release,
                             "paper": paper, "color": color, "duplex": duplex,
                             "service": service})


def example(jobs, *, printers=None, horizon=1000, stock=None):
    printers = printers or stress.default_fleet(10)[:2]
    return {"name": "baseline rule fixture", "time_unit": "ms", "jobs": jobs,
            "printers": printers, "horizon_ms": horizon,
            "inventory": stock or {"a4": 10000, "a3": 3000},
            "processing_ms": [[stress.processing_ms(j, p) if stress.compatible(j, p) else None for p in printers] for j in jobs],
            "cost_cents": [[stress.direct_cost(j, p) if stress.compatible(j, p) else None for p in printers] for j in jobs]}


class StressScheduleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = stress.generate_document()
        cls.data = cls.document["scenarios"][0]

    def test_fixed_seed_and_schema(self):
        self.assertEqual(self.document, stress.generate_document())
        self.assertNotEqual(self.data["jobs"], stress.generate_scenario(stress.SEED + 1)["jobs"])
        self.assertEqual(self.document["kind"], "campus-print-optimal-input")
        self.assertEqual(self.document["objective"], {"name": "balanced-shop-quality-v1",
                         "wait_reference_ms": 1800000, "waiting_weight": 1,
                         "express_weight": 1, "contribution_weight": 1})
        self.assertEqual(len(self.data["jobs"]), 100)
        self.assertEqual(len(self.data["printers"]), 6)
        self.assertEqual(len({j["job_id"] for j in self.data["jobs"]}), 100)
        self.assertFalse(self.data["generation"]["maximum_complexity_claim"])

    def test_default_fleet_is_production_configuration(self):
        fleet = self.data["printers"]
        self.assertEqual([(p["mono_page_ms"], p["color_page_ms"]) for p in fleet],
                         [(1250, 1250), (2500, 2500), (3750, 3750),
                          (2500, 3750), (1250, 2500), (2500, 5000)])
        self.assertEqual([i for i, p in enumerate(fleet) if p["a3"]], [5])
        self.assertEqual([i for i, p in enumerate(fleet) if p["color"]], [3, 4, 5])
        self.assertEqual([i for i, p in enumerate(fleet) if not p["duplex"]], [2])

    def test_quote_and_per_copy_duplex_rounding(self):
        job = order("odd", pages=5, copies=3, paper="a3", duplex=True, service="express")
        self.assertEqual(stress.paper_sheets(job), 9)  # ceil(5/2)*3, not ceil(15/2)
        self.assertEqual(job["base_cents"], 600)
        self.assertEqual(job["quoted_cents"], 570)
        self.assertEqual(job["nominal_cost_cents"], 204)
        self.assertEqual(job["deadline_ms"], 1800000)
        color = order("colour", pages=5, copies=3, paper="a3", color=True,
                      duplex=True, service="standard", release=123)
        self.assertEqual(color["base_cents"], 4500)
        self.assertEqual(color["quoted_cents"], 4061)
        self.assertEqual(color["nominal_cost_cents"], 1404)
        self.assertEqual(color["deadline_ms"], 7200123)

    def test_bottlenecks_and_wide_sizes_are_present(self):
        summary = self.data["workload_summary"]
        self.assertEqual(summary, stress.workload_summary(self.data))
        self.assertEqual(summary["total_faces"], 69394)
        self.assertGreater(summary["fastest_work_fleet_horizon_ratio"], 4)
        self.assertLess(summary["fastest_work_fleet_horizon_ratio"], 6)
        self.assertGreater(summary["a3_single_printer_work_horizon_ratio"], 10)
        self.assertGreater(summary["paper_demand_stock_ratio"]["a4"], 3)
        self.assertGreater(summary["paper_demand_stock_ratio"]["a3"], 3)
        self.assertGreater(summary["single_compatible_printer_orders"], 10)
        self.assertGreater(summary["color_orders"], 20)
        self.assertGreater(summary["duplex_orders"], 20)
        self.assertEqual(set(summary["tiers"]), {"express", "standard", "flexible"})
        self.assertLessEqual(summary["min_faces"], 3)
        self.assertGreater(summary["max_faces"], 5000)
        self.assertEqual(summary["arrived_at_zero"], 35)
        self.assertGreater(summary["arrival_timestamps"], 10)
        self.assertTrue(all(0 <= j["submitted_ms"] < self.data["horizon_ms"] for j in self.data["jobs"]))

    def test_matrices_follow_capabilities_speeds_and_costs(self):
        for i, job in enumerate(self.data["jobs"]):
            faces = job["pages"] * job["copies"]
            self.assertLessEqual(faces, 10000)
            self.assertGreaterEqual(job["pages"], 1)
            self.assertLessEqual(job["pages"], 1000)
            self.assertLessEqual(job["copies"], 100)
            for m, printer in enumerate(self.data["printers"]):
                duration, cost = self.data["processing_ms"][i][m], self.data["cost_cents"][i][m]
                if not stress.compatible(job, printer):
                    self.assertIsNone(duration)
                    self.assertIsNone(cost)
                else:
                    self.assertEqual(duration, stress.processing_ms(job, printer))
                    self.assertEqual(cost, stress.direct_cost(job, printer))
                    self.assertGreaterEqual(job["quoted_cents"], cost)

    def test_fcfs_and_sjf_have_production_queue_and_machine_ties(self):
        printers = stress.default_fleet(10)[:2]
        printers[0]["mono_page_ms"] = 50
        printers[1]["mono_page_ms"] = 10
        data = example([order("long", pages=5), order("short-a"), order("short-b")], printers=printers)
        fcfs = stress.replay_fcfs_sjf(data, "fcfs")
        self.assertEqual([(d["job_id"], d["printer"], d["started_ms"]) for d in fcfs],
                         [("long", 0, 0), ("short-a", 1, 0), ("short-b", 1, 10)])
        sjf = stress.replay_fcfs_sjf(data, "sjf")
        self.assertEqual([(d["job_id"], d["printer"], d["started_ms"]) for d in sjf],
                         [("short-a", 1, 0), ("short-b", 0, 0), ("long", 1, 10)])
        with self.assertRaises(ValueError):
            stress.replay_fcfs_sjf(data, "unknown")

    def test_blocks_are_skipped_and_stock_reserved_for_crossing_job(self):
        data = example([order("a3-blocked", paper="a3"), order("long", pages=10),
                        order("later", release=1)], printers=stress.default_fleet(10)[:1],
                       horizon=50, stock={"a4": 10, "a3": 3000})
        trace = stress.replay_fcfs_sjf(data, "fcfs")
        self.assertEqual([d["job_id"] for d in trace], ["long"])
        self.assertEqual(trace[0]["finished_ms"], 100)
        quality = evaluate(data, trace)
        self.assertEqual(quality["completed"], 0)
        self.assertEqual(quality["contribution_cents"], 0)
        self.assertEqual(quality["average_observed_wait_ms_fraction"], "33")

    def test_exact_closing_boundary(self):
        data = example([order("at-close", release=50), order("before-close", release=40)],
                       printers=stress.default_fleet(10)[:1], horizon=50)
        trace = stress.replay_fcfs_sjf(data)
        self.assertEqual([d["job_id"] for d in trace], ["before-close"])
        self.assertEqual(trace[0]["finished_ms"], 50)
        self.assertEqual(evaluate(data, trace)["completed"], 1)

    def test_nearest_rank_p95_for_100_orders(self):
        # Jobs started at arrival wait zero.  The other jobs observe H-r=100.
        for dispatched, expected in ((94, 100), (95, 0)):
            jobs = [order(str(i), release=i if i < dispatched else 0) for i in range(100)]
            data = example(jobs, printers=stress.default_fleet(10)[:1], horizon=100)
            data["processing_ms"] = [[1] for _ in jobs]
            trace = [{"job_id": str(i), "printer": 0, "started_ms": i,
                      "finished_ms": i + 1} for i in range(dispatched)]
            self.assertEqual(evaluate(data, trace)["p95_observed_wait_ms"], expected)

    def test_baselines_are_feasible_and_reproduce_exact_quality(self):
        before = copy.deepcopy(self.data)
        self.assertEqual([b["policy"] for b in self.data["baselines"]], ["fcfs", "sjf"])
        for baseline in self.data["baselines"]:
            trace = stress.replay_fcfs_sjf(self.data, baseline["policy"])
            self.assertEqual(trace, baseline["dispatches"])
            self.assertEqual(evaluate(self.data, trace), baseline["quality"])
            self.assertGreater(baseline["quality"]["completed"], 20)
            self.assertLess(baseline["quality"]["completed"], 100)
            self.assertTrue(all(d["started_ms"] < self.data["horizon_ms"] for d in trace))
            Fraction(baseline["quality"]["quality_score_fraction"])
        self.assertEqual(self.data, before)

    def test_cli_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "input.json"
            command = [sys.executable, str(ROOT / "tools" / "generate_stress_schedule.py"), "--output", str(output)]
            created = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(created.returncode, 0, created.stderr)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), self.document)
            before = output.read_bytes()
            repeated = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(repeated.returncode, 0)
            self.assertEqual(output.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
