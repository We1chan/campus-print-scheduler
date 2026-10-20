"""Audit the exact offline solver with an independent integer-time oracle."""
import argparse
import copy
from fractions import Fraction
import importlib.util
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOLVER_PATH = ROOT / "tools" / "solve_optimal_schedule.py"
SPEC = importlib.util.spec_from_file_location("solve_optimal_schedule", SOLVER_PATH)
SOLVER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SOLVER
SPEC.loader.exec_module(SOLVER)
REFERENCE_MS = 1_800_000
EXPORTER = None


def document(data):
    return {"schema_version": 1, "kind": "campus-print-optimal-input",
            "objective": {"name": "balanced-shop-quality-v1",
                          "wait_reference_ms": REFERENCE_MS, "waiting_weight": 1,
                          "express_weight": 1, "contribution_weight": 1},
            "scenarios": [data]}


def job(ident, *, release=0, quote=10, service="standard", deadline=0,
        pages=1, copies=1, paper="a4", duplex=False, nominal=0, refund=0):
    return {
        "job_id": ident, "title": ident, "pages": pages, "copies": copies,
        "submitted_ms": release, "paper": paper, "color": False,
        "duplex": duplex, "service": service, "deadline_ms": deadline,
        "quoted_cents": quote, "refund_cents": refund,
        "nominal_cost_cents": nominal,
    }


def scenario(jobs, processing, *, horizon=5, stock=100, costs=None):
    count = len(processing[0]) if processing else 1
    return {
        "name": "独立审计 小例子", "horizon_ms": horizon, "jobs": jobs,
        "printers": [{"name": f"打印机 {i}", "online": True,
                      "a3": True, "color": True, "duplex": True}
                     for i in range(count)],
        "inventory": {"a4": stock, "a3": stock},
        "processing_ms": processing,
        "cost_cents": costs if costs is not None else
            [[0 if duration is not None else None for duration in row]
             for row in processing],
    }


def dispatch(ident, printer, start, duration):
    return {"job_id": ident, "printer": printer,
            "started_ms": start, "finished_ms": start + duration}


def independent_score(data, choices):
    """Compute the documented score directly, without calling solver helpers.

    A choice is None or (printer, integer start), indexed by input order.
    This function also checks inventory and every pair of occupied intervals.
    """
    jobs, horizon = data["jobs"], data["horizon_ms"]
    if not jobs:
        return Fraction(0)
    consumed = {"a4": 0, "a3": 0}
    intervals = [[] for _ in data["printers"]]
    waits, profit, express, on_time = [], 0, 0, 0
    for index, (order, choice) in enumerate(zip(jobs, choices)):
        express += order["service"] == "express"
        if choice is None:
            waits.append(horizon - order["submitted_ms"])
            continue
        printer, start = choice
        duration = data["processing_ms"][index][printer]
        if duration is None or not order["submitted_ms"] <= start < horizon:
            return None
        finish = start + duration
        for previous_start, previous_finish in intervals[printer]:
            if start < previous_finish and previous_start < finish:
                return None
        intervals[printer].append((start, finish))
        sheets = (order["pages"] + 1) // 2 if order["duplex"] else order["pages"]
        consumed[order["paper"]] += sheets * order["copies"]
        waits.append(start - order["submitted_ms"])
        if finish <= horizon:
            profit += (order["quoted_cents"] - order["refund_cents"] -
                       data["cost_cents"][index][printer])
            if (order["service"] == "express" and order["deadline_ms"] > 0
                    and finish <= order["deadline_ms"]):
                on_time += 1
    if any(consumed[paper] > data["inventory"][paper] for paper in consumed):
        return None
    average = Fraction(sum(waits), len(waits))
    p95 = sorted(waits)[(95 * len(waits) + 99) // 100 - 1]
    waiting = (Fraction(1, 2) / (1 + average / REFERENCE_MS) +
               Fraction(1, 2) / (1 + Fraction(p95, REFERENCE_MS)))
    express_score = Fraction(on_time, express) if express else Fraction(1)
    potential = sum(max(0, order["quoted_cents"] - order["nominal_cost_cents"])
                    for order in jobs)
    contribution = (min(Fraction(1), max(Fraction(0), Fraction(profit, potential)))
                    if potential else Fraction(0))
    return 100 * (waiting + express_score + contribution) / 3


def time_grid_oracle(data):
    """Try *all* integer starts, including intentional idle time and omissions.

    Unlike the production search, this does not enumerate machine sequences or
    apply left justification. Small horizons make exhaustive time-grid search
    practical and provide an independent check of that reduction.
    """
    options = []
    for index, order in enumerate(data["jobs"]):
        options.append([None] + [
            (printer, start)
            for printer, duration in enumerate(data["processing_ms"][index])
            if duration is not None
            for start in range(order["submitted_ms"], data["horizon_ms"])
        ])
    best = Fraction(-1)
    for choices in itertools.product(*options):
        score = independent_score(data, choices)
        if score is not None:
            best = max(best, score)
    return best


class ExactScheduleTests(unittest.TestCase):
    def assert_optimal(self, data):
        result = SOLVER.solve_scenario(data, max_nodes=1_000_000)
        self.assertEqual(result["status"], "OPTIMAL")
        self.assertTrue(result["proof"]["complete"])
        quality = SOLVER.evaluate(data, result["dispatches"])
        score = Fraction(quality["quality_score_fraction"])
        self.assertEqual(score, Fraction(result["quality"]["quality_score_fraction"]))
        self.assertEqual(Fraction(result["proof"]["lower_bound_fraction"]), score)
        self.assertEqual(Fraction(result["proof"]["upper_bound_fraction"]), score)
        self.assertEqual(Fraction(result["proof"]["absolute_gap_fraction"]), 0)
        return result

    def test_future_express_order_requires_intentional_idle_time(self):
        data = scenario([
            job("long", quote=10, service="flexible"),
            job("express", release=1, quote=1, service="express", deadline=2),
        ], [[5], [1]])
        result = self.assert_optimal(data)
        starts = {item["job_id"]: item["started_ms"] for item in result["dispatches"]}
        self.assertEqual(starts["express"], 1)
        self.assertEqual(starts["long"], 2)
        self.assertEqual(result["quality"]["express_on_time"], 1)
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]),
                         time_grid_oracle(data))

    def test_heterogeneous_assignment_preserves_scarce_capability(self):
        orders = [job("ordinary"), job("color")]
        orders[1]["color"] = True
        data = scenario(orders, [[2, 1], [None, 1]], horizon=2)
        data["printers"][0]["color"] = False
        result = self.assert_optimal(data)
        placed = {item["job_id"]: (item["printer"], item["started_ms"])
                  for item in result["dispatches"]}
        self.assertEqual(placed, {"ordinary": (0, 0), "color": (1, 0)})
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), 100)

    def test_limited_inventory_selects_value_instead_of_input_order(self):
        data = scenario([job("cheap", quote=1), job("valuable", quote=9)],
                        [[1], [1]], stock=1, horizon=3)
        result = self.assert_optimal(data)
        self.assertEqual([item["job_id"] for item in result["dispatches"]], ["valuable"])
        self.assertEqual(result["quality"]["contribution_cents"], 9)

    def test_duplex_stock_rounds_each_copy_separately(self):
        data = scenario([job("duplex", pages=3, copies=2, duplex=True)],
                        [[1]], stock=3)
        result = self.assert_optimal(data)
        self.assertEqual(result["dispatches"], [])
        data["inventory"]["a4"] = 4
        result = self.assert_optimal(data)
        self.assertEqual(len(result["dispatches"]), 1)

    def test_horizon_completion_is_inclusive_but_start_is_exclusive(self):
        data = scenario([job("boundary", service="express", deadline=2)],
                        [[2]], horizon=2)
        quality = SOLVER.evaluate(data, [dispatch("boundary", 0, 0, 2)])
        self.assertEqual(quality["completed"], 1)
        self.assertEqual(quality["express_on_time"], 1)
        self.assertEqual(Fraction(quality["quality_score_fraction"]), 100)
        with self.assertRaises(ValueError):
            SOLVER.evaluate(data, [dispatch("boundary", 0, 2, 2)])

    def test_unstarted_orders_wait_until_close_and_running_orders_do_not_settle(self):
        data = scenario([job("blocked", paper="a3"), job("running", release=2)],
                        [[None], [20]], horizon=10)
        data["printers"][0]["a3"] = False
        quality = SOLVER.evaluate(data, [dispatch("running", 0, 3, 20)])
        self.assertEqual(quality["average_observed_wait_ms"], 5.5)
        self.assertEqual(quality["p95_observed_wait_ms"], 10)
        self.assertEqual(quality["completed"], 0)
        self.assertEqual(quality["contribution_cents"], 0)
        unstarted = SOLVER.evaluate(data, [])
        self.assertEqual(unstarted["average_observed_wait_ms"], 9)
        self.assertEqual(unstarted["p95_observed_wait_ms"], 10)

    def test_p95_uses_nearest_rank_instead_of_interpolation_or_maximum(self):
        orders = [job(f"order-{index}") for index in range(20)]
        data = scenario(orders, [[1]] * 20, horizon=25)
        quality = SOLVER.evaluate(data, [dispatch(order["job_id"], 0, index, 1)
                                         for index, order in enumerate(orders)])
        self.assertEqual(quality["average_observed_wait_ms"], 9.5)
        self.assertEqual(quality["p95_observed_wait_ms"], 18)

    def test_any_compatible_negative_net_contribution_is_rejected(self):
        data = scenario([job("loss", quote=5)], [[1, 1]], costs=[[0, 6]])
        with self.assertRaises(ValueError):
            SOLVER.solve_scenario(data)
        with self.assertRaises(ValueError):
            SOLVER.evaluate(data, [])

    def test_node_budget_never_claims_unproved_optimality(self):
        data = scenario([
            job("long", quote=10, service="flexible"),
            job("express", release=1, quote=1, service="express", deadline=2),
        ], [[5], [1]])
        result = SOLVER.solve_scenario(data, max_nodes=1)
        self.assertEqual(result["status"], "FEASIBLE")
        self.assertFalse(result["proof"]["complete"])
        lower = Fraction(result["proof"]["lower_bound_fraction"])
        upper = Fraction(result["proof"]["upper_bound_fraction"])
        self.assertLess(lower, upper)
        self.assertLessEqual(lower, time_grid_oracle(data))
        self.assertGreaterEqual(upper, time_grid_oracle(data))
        self.assertEqual(Fraction(result["proof"]["absolute_gap_fraction"]), upper - lower)
        SOLVER.evaluate(data, result["dispatches"])

    def test_duplicate_jobs_and_duplicate_or_overlapping_dispatches_are_rejected(self):
        duplicate = scenario([job("same"), job("same")], [[1], [1]])
        with self.assertRaises(ValueError):
            SOLVER.solve_scenario(duplicate)
        data = scenario([job("one"), job("two")], [[2], [2]])
        for trace in ([dispatch("one", 0, 0, 2), dispatch("one", 0, 2, 2)],
                      [dispatch("one", 0, 0, 2), dispatch("two", 0, 1, 2)],
                      [dispatch("unknown", 0, 0, 2)],
                      [dispatch("one", 0, 0, 1)]):
            with self.subTest(trace=trace), self.assertRaises(ValueError):
                SOLVER.evaluate(data, trace)

    def test_empty_set_has_zero_score_and_zero_gap(self):
        result = self.assert_optimal(scenario([], [], horizon=5))
        self.assertEqual(result["dispatches"], [])
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), 0)
        self.assertEqual(result["quality"]["average_observed_wait_ms"], 0)
        self.assertEqual(result["quality"]["p95_observed_wait_ms"], 0)

    def test_exact_solver_matches_independent_all_start_times_oracle(self):
        rng = random.Random(20261020)
        for index in range(24):
            count = 1 + index % 3
            machines = 1 + index % 2
            horizon = 3 + index % 3
            orders, processing, costs = [], [], []
            printers = [{"name": f"p-{machine}", "online": bool(rng.randrange(4)),
                         "a3": bool(rng.randrange(2)), "color": bool(rng.randrange(2)),
                         "duplex": bool(rng.randrange(2))} for machine in range(machines)]
            for ident in range(count):
                quote = rng.randint(1, 10)
                release = rng.randrange(horizon)
                order = job(f"j-{ident}", release=release, quote=quote,
                            nominal=rng.randrange(quote + 1),
                            service=rng.choice(["standard", "express", "flexible"]),
                            deadline=rng.randint(max(1, release), horizon + 1),
                            paper=rng.choice(["a4", "a3"]), duplex=bool(rng.randrange(2)))
                order["color"] = bool(rng.randrange(2))
                orders.append(order)
                row = [rng.randint(1, horizon + 1)
                       if (printer["online"] and (order["paper"] == "a4" or printer["a3"])
                           and (not order["color"] or printer["color"])
                           and (not order["duplex"] or printer["duplex"])) else None
                       for printer in printers]
                processing.append(row)
                costs.append([rng.randrange(quote + 1) if duration is not None else None
                              for duration in row])
            data = scenario(orders, processing, costs=costs, horizon=horizon,
                            stock=rng.randint(0, count))
            data["printers"] = printers
            with self.subTest(index=index, data=data):
                expected = time_grid_oracle(data)
                result = self.assert_optimal(data)
                self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), expected)

    def test_three_orders_parallel_time_grid_includes_every_idle_start(self):
        data = scenario([
            job("ordinary", quote=6, nominal=1),
            job("express", release=1, quote=7, service="express", deadline=3),
            job("flexible", quote=2, service="flexible"),
        ], [[4, 2], [2, 3], [1, 2]], costs=[[2, 3], [1, 2], [1, 1]],
            stock=3, horizon=5)
        expected = time_grid_oracle(data)
        result = self.assert_optimal(data)
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), expected)

    def test_input_is_not_mutated(self):
        data = scenario([job("immutable")], [[1]])
        before = copy.deepcopy(data)
        result = self.assert_optimal(data)
        SOLVER.evaluate(data, result["dispatches"])
        self.assertEqual(data, before)

    def test_cli_handles_chinese_spaces_and_refuses_existing_output(self):
        data = scenario([job("cli")], [[1]])
        with tempfile.TemporaryDirectory(prefix="最优 排程-") as root:
            input_path = Path(root) / "教学 输入.json"
            output_path = Path(root) / "最优 结果.json"
            raw = (json.dumps(document(data), ensure_ascii=False) + "\n").encode("utf-8")
            input_path.write_bytes(raw)
            command = [sys.executable, str(SOLVER_PATH), "--input", str(input_path),
                       "--output", str(output_path)]
            first = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn(data["name"], first.stdout.decode("utf-8"))
            report = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(report["scenarios"][0]["status"], "OPTIMAL")
            self.assertEqual(report["scenarios"][0]["name"], data["name"])
            self.assertEqual(Fraction(report["scenarios"][0]["quality"]["quality_score_fraction"]), 100)
            self.assertEqual(report["evidence"]["input_sha256"], hashlib.sha256(raw).hexdigest())
            before = output_path.read_bytes()
            second = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(second.returncode, 1)
            self.assertEqual(output_path.read_bytes(), before)
            same_path = subprocess.run(
                [sys.executable, str(SOLVER_PATH), "--input", str(input_path),
                 "--output", str(input_path)], capture_output=True, timeout=20)
            self.assertEqual(same_path.returncode, 1)
            self.assertEqual(input_path.read_bytes(), raw)

    def test_current_cpp_teaching_orders_and_baselines(self):
        if not EXPORTER:
            self.skipTest("pass --exporter to audit the compiled teaching data exporter")
        expected = {
            "长单与短单": "100932850/1085139",
            "临近取件期限": "100932850/1085139",
            "服务档位与等待老化": "722985500/7707843",
            "营业高峰与收益": "374843450/3955149",
        }
        with tempfile.TemporaryDirectory(prefix="教学 最优-") as root:
            input_path = Path(root) / "真实 教学输入.json"
            subprocess.run([EXPORTER, "--output", str(input_path)],
                           check=True, capture_output=True, timeout=20)
            actual = json.loads(input_path.read_text(encoding="utf-8"))
            self.assertEqual({data["name"] for data in actual["scenarios"]}, set(expected))
            result = SOLVER.solve_document(actual)
            for data, solved in zip(actual["scenarios"], result["scenarios"]):
                with self.subTest(name=data["name"]):
                    self.assertEqual(solved["status"], "OPTIMAL")
                    optimum = Fraction(solved["quality"]["quality_score_fraction"])
                    self.assertEqual(optimum, Fraction(expected[data["name"]]))
                    self.assertEqual(Fraction(solved["proof"]["absolute_gap_fraction"]), 0)
                    self.assertEqual({baseline["policy"] for baseline in data["baselines"]},
                                     {"fcfs", "sjf"})
                    for baseline in data["baselines"]:
                        by_id = {item["job_id"]: (item["printer"], item["started_ms"])
                                 for item in baseline["dispatches"]}
                        score = independent_score(data, [by_id.get(order["job_id"])
                                                         for order in data["jobs"]])
                        self.assertIsNotNone(score)
                        self.assertAlmostEqual(float(score), baseline["quality"]["overall_score"], places=10)
                        self.assertGreaterEqual(optimum, score)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--exporter")
    options, remaining = parser.parse_known_args()
    EXPORTER = options.exporter
    unittest.main(argv=[sys.argv[0], *remaining])
