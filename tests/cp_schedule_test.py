"""Compare CP-SAT certification with exact enumeration and the production core.

OR-Tools is optional: mathematical solver checks skip when it is unavailable.
Pass --replay <stress-replay executable> for independent C++ generator checks.
"""
import argparse
import copy
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_stress_schedule as stress
import solve_cp_schedule as cp
import solve_optimal_schedule as exact

HAS_ORTOOLS = importlib.util.find_spec("ortools") is not None
REPLAY = None


def job(ident, *, release=0, pages=1, copies=1, paper="a4", color=False,
        duplex=False, service="standard", quote=10, nominal=0, deadline=0):
    return {"job_id": ident, "title": ident, "pages": pages, "copies": copies,
            "submitted_ms": release, "paper": paper, "color": color,
            "duplex": duplex, "service": service, "deadline_ms": deadline,
            "quoted_cents": quote, "refund_cents": 0, "nominal_cost_cents": nominal}


def fixture(jobs, durations, *, horizon=20, costs=None, stock=None, printers=None):
    count = len(durations[0]) if durations else 1
    return {"name": "CP exact audit", "time_unit": "ms", "horizon_ms": horizon,
            "jobs": jobs, "processing_ms": durations,
            "cost_cents": costs if costs is not None else [[0 if t is not None else None for t in row] for row in durations],
            "inventory": stock or {"a4": 10000, "a3": 3000},
            "printers": printers or [{"name": f"printer-{m}", "online": True,
                                      "a3": True, "color": True, "duplex": True}
                                     for m in range(count)]}


def teaching_fixtures():
    """The four six-order display-clock fixtures in merchant_dispatch_model.cpp.

    Constants live in this test so the comparison does not depend on ignored
    runtime files or a working Qt installation.  Pricing uses the separately
    audited production generator; neither solver generates the expected score.
    """
    names = ("长单与短单", "临近取件期限", "服务档位与等待老化", "营业高峰与收益")
    releases = ((0, 0, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0),
                (0, 0, 1, 2, 3, 4), (0, 0, 0, 1, 1, 2))
    deadlines = ((12, 3, 8, 5, 0, 2), (4, 2, 8, 5, 10, 1),
                 (9, 6, 8, 7, 0, 6), (11, 3, 8, 5, 0, 4))
    pages = (12, 2, 6, 3, 8, 1)
    printer = {"name": "教学打印机", "online": True, "a3": False,
               "color": True, "duplex": True}
    scenarios = []
    for case, name in enumerate(names):
        orders, costs = [], []
        for i, count in enumerate(pages):
            tier = "flexible" if i in (0, 4) else "standard" if i == 2 else "express"
            order = stress.quote_job(job(chr(65 + i), pages=count,
                                         release=releases[case][i] * 60000,
                                         color=i in (3, 5) or (case == 3 and i == 4),
                                         service=tier))
            order["deadline_ms"] = deadlines[case][i] * 60000
            orders.append(order)
            costs.append([stress.direct_cost(order, printer)])
        scene = fixture(orders, [[p * 20000] for p in pages],
                        horizon=640000 + max(releases[case]) * 60000,
                        costs=costs, printers=[printer])
        scene["name"] = name
        scenarios.append(scene)
    return scenarios


@unittest.skipUnless(HAS_ORTOOLS, "optional OR-Tools dependency is unavailable")
class CpScheduleTest(unittest.TestCase):
    def assertCertified(self, scene, *, seconds=4):
        expected = exact.solve_scenario(scene, max_nodes=1000000)
        self.assertEqual(expected["status"], "OPTIMAL")
        result = cp.solve_scenario_cp(scene, time_limit_s=seconds, workers=1)
        self.assertEqual(result["status"], "OPTIMAL", result["proof"])
        proof = result["proof"]
        score = Fraction(result["quality"]["quality_score_fraction"])
        self.assertEqual(score, Fraction(expected["quality"]["quality_score_fraction"]))
        self.assertEqual(exact.evaluate(scene, result["dispatches"]), result["quality"])
        self.assertTrue(proof["complete"])
        self.assertEqual(Fraction(proof["lower_bound_fraction"]), score)
        self.assertEqual(Fraction(proof["upper_bound_fraction"]), score)
        self.assertEqual(Fraction(proof["absolute_gap_fraction"]), 0)
        if scene["jobs"]:
            self.assertEqual(proof["phase_two_status"], "INFEASIBLE")
            self.assertEqual(proof["termination"], "all_potentially_better_aggregate_tuples_exhausted")
            self.assertTrue(proof["aggregate_tuples"])
            self.assertTrue(any(s["phase"] == "exact_aggregate_exhaustion" and s["status"] == "INFEASIBLE"
                                for s in proof["solves"]))
            for audited in proof["aggregate_tuples"]:
                self.assertEqual(len(audited["dispatches_sha256"]), 64)
                int(audited["dispatches_sha256"], 16)
                self.assertGreater(Fraction(audited["quality_score_fraction"]), 0)
        return result

    def test_all_four_current_teaching_fixtures(self):
        expected = ("100932850/1085139", "100932850/1085139",
                    "722985500/7707843", "374843450/3955149")
        for scene, score in zip(teaching_fixtures(), expected):
            with self.subTest(scene=scene["name"]):
                result = self.assertCertified(scene, seconds=5)
                self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), Fraction(score))

    def test_random_physical_compatibility_one_to_four_orders(self):
        rng = random.Random(219104)
        for case in range(12):
            fleet = stress.default_fleet(10)
            # Retain heterogeneous mono, colour and sole-A3 options, with
            # different speeds and costs; some cases also include a simplex unit.
            fleet = [fleet[i] for i in ((0, 3, 5) if case % 2 else (2, 4, 5))]
            orders = []
            for i in range(1 + case % 4):
                order = stress.quote_job(job(f"{case}-{i}", pages=rng.randint(1, 5),
                    copies=rng.randint(1, 2), release=rng.randrange(0, 10) * 10,
                    paper=rng.choice(("a4", "a4", "a3")), color=bool(rng.getrandbits(1)),
                    duplex=bool(rng.getrandbits(1)), service=rng.choice(tuple(stress.WINDOW_MS))))
                order["deadline_ms"] = order["submitted_ms"] + rng.randrange(1, 15) * 10
                orders.append(order)
            durations = [[stress.processing_ms(j, p) if stress.compatible(j, p) else None for p in fleet] for j in orders]
            costs = [[stress.direct_cost(j, p) if stress.compatible(j, p) else None for p in fleet] for j in orders]
            scene = fixture(orders, durations, costs=costs, printers=fleet,
                            horizon=250, stock={"a4": rng.randint(2, 14), "a3": rng.randint(2, 14)})
            with self.subTest(case=case):
                self.assertCertified(scene)

    def test_tiny_budget_keeps_safe_nonoptimal_bounds(self):
        scene = stress.generate_scenario()
        result = cp.solve_scenario_cp(scene, time_limit_s=1e-9, workers=1)
        self.assertEqual(result["status"], "FEASIBLE")
        self.assertFalse(result["proof"]["complete"])
        lower = Fraction(result["proof"]["lower_bound_fraction"])
        upper = Fraction(result["proof"]["upper_bound_fraction"])
        self.assertEqual(lower, Fraction(result["quality"]["quality_score_fraction"]))
        self.assertLessEqual(lower, upper)
        self.assertLessEqual(upper, 100)
        self.assertGreater(Fraction(result["proof"]["absolute_gap_fraction"]), 0)
        self.assertEqual(exact.evaluate(scene, result["dispatches"]), result["quality"])

    def test_floor_band_certifies_original_rational_objective(self):
        scene = fixture([job("a", pages=3), job("b", release=2)], [[7], [3]], horizon=20)
        result = self.assertCertified(scene)
        proof, quality = result["proof"], result["quality"]
        expected_floor = (6 * proof["scale"] * Fraction(quality["quality_score_fraction"]) / 100).__floor__()
        self.assertLessEqual(proof["aggregate_search_initial_threshold"], expected_floor - 3)
        for audited in proof["aggregate_tuples"]:
            exact_scaled = 6 * proof["scale"] * Fraction(audited["quality_score_fraction"]) / 100
            self.assertLessEqual(audited["scaled_objective"], exact_scaled)
            self.assertLess(exact_scaled, audited["scaled_objective"] + 4)
        normalized = sorted(result["dispatches"], key=lambda d: (d["started_ms"], d["printer"], d["job_id"]))
        digest = hashlib.sha256(json.dumps(normalized, sort_keys=True).encode("utf-8")).hexdigest()
        self.assertIn(digest, [a["dispatches_sha256"] for a in proof["aggregate_tuples"]])

    def test_p95_is_exact_nearest_rank_for_one_hundred_orders(self):
        printer = {"name": "offline", "online": False, "a3": True, "color": True, "duplex": True}
        for zero_waits, expected in ((94, 100), (95, 0)):
            orders = [job(str(i), release=100 if i < zero_waits else 0) for i in range(100)]
            scene = fixture(orders, [[None] for _ in orders], horizon=100, printers=[printer])
            result = cp.solve_scenario_cp(scene, time_limit_s=2, workers=1)
            self.assertEqual(result["status"], "OPTIMAL", result["proof"])
            self.assertEqual(result["quality"]["p95_observed_wait_ms"], expected)
            self.assertEqual(result["quality"], exact.evaluate(scene, []))

    def test_zero_potential_no_express_capped_profit_and_empty_cohort(self):
        zero = fixture([job("zero", quote=5, nominal=5)], [[1]], horizon=5)
        result = self.assertCertified(zero)
        self.assertEqual(result["quality"]["contribution_score"], 0)
        self.assertEqual(result["quality"]["express_on_time_rate"], 1)
        capped = fixture([job("capped", quote=10, nominal=9)], [[1]], horizon=5)
        result = self.assertCertified(capped)
        self.assertEqual(result["quality"]["contribution_score"], 1)
        self.assertGreater(result["quality"]["contribution_cents"], result["quality"]["potential_contribution_cents"])
        empty = fixture([], [], horizon=5)
        self.assertEqual(self.assertCertified(empty)["quality"]["quality_score_fraction"], "0")

    def test_reference_clock_tick_conversion(self):
        scene = fixture([job("a"), job("b")], [[600], [600]], horizon=2400)
        result = self.assertCertified(scene)
        self.assertEqual(result["proof"]["tick_ms"], 600)
        self.assertEqual(result["quality"]["average_observed_wait_ms_fraction"], "300")
        self.assertEqual(result["quality"]["p95_observed_wait_ms"], 600)
        expected = 100 * ((Fraction(1800000, 1800300) + Fraction(1800000, 1800600)) / 2 + 2) / 3
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]), expected)

    def test_crossing_close_reserves_paper_despite_zero_completion(self):
        scene = fixture([job("early", pages=2), job("later", release=10)],
                        [[100, 100], [100, 100]], horizon=50, stock={"a4": 2, "a3": 0})
        result = self.assertCertified(scene)
        self.assertEqual([d["job_id"] for d in result["dispatches"]], ["early"])
        self.assertEqual(result["dispatches"][0]["started_ms"], 0)
        self.assertGreater(result["dispatches"][0]["finished_ms"], scene["horizon_ms"])
        self.assertEqual(result["quality"]["completed"], 0)
        self.assertEqual(result["quality"]["contribution_cents"], 0)

    def test_intentional_idle_is_allowed(self):
        scene = fixture([job("long", pages=10), job("short", release=2)], [[10], [1]], horizon=20)
        result = self.assertCertified(scene)
        self.assertEqual([(d["job_id"], d["started_ms"], d["finished_ms"]) for d in result["dispatches"]],
                         [("short", 2, 3), ("long", 3, 13)])

    def test_component_optima_reused_without_changing_original_objective(self):
        scene = fixture([job("long", pages=10), job("express", release=2,
                        service="express", deadline=5)], [[10], [1]], horizon=20)
        records = []
        for component, sense, unit in (("timely", "max", "count"), ("p95", "min", "ticks")):
            with self.subTest(component=component):
                diagnostic = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1,
                                                  objective_component=component)
                self.assertEqual(diagnostic["status"], "FEASIBLE")
                self.assertFalse(diagnostic["proof"]["complete"])
                self.assertEqual(diagnostic["proof"]["phase_one_status"], "OPTIMAL")
                self.assertIsNone(diagnostic["proof"]["phase_two_status"])
                self.assertEqual(diagnostic["proof"]["termination"], "component_optimal")
                bound = diagnostic["component_bound"]
                self.assertEqual((bound["sense"], bound["unit"]), (sense, unit))
                self.assertEqual(bound["solver_status"], "OPTIMAL")
                self.assertEqual(bound["primal_value"], bound["certified_bound"])
                self.assertEqual(bound["scenario_sha256"], cp.scenario_sha256(scene))
                self.assertEqual(bound["quality"], exact.evaluate(scene, bound["dispatches"]))
                records.append(bound)
        result = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, certified_bounds=records)
        expected = exact.solve_scenario(scene)
        self.assertEqual(result["status"], "OPTIMAL", result["proof"])
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]),
                         Fraction(expected["quality"]["quality_score_fraction"]))
        self.assertEqual(result["proof"]["certified_component_bounds"], records)
        relaxation = Fraction(result["proof"]["component_relaxation_upper_bound_fraction"])
        self.assertGreaterEqual(relaxation, Fraction(result["quality"]["quality_score_fraction"]))

    def test_component_hash_tick_sense_unit_and_primal_mismatches_rejected(self):
        scene = fixture([job("a", service="express", deadline=4)], [[2]], horizon=10)
        bound = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1,
                                     objective_component="timely")["component_bound"]
        alterations = (("scenario_sha256", "0" * 64, "hash mismatch"),
                       ("tick_ms", bound["tick_ms"] + 1, "time unit mismatch"),
                       ("sense", "min", "sense/unit mismatch"),
                       ("unit", "ticks", "sense/unit mismatch"),
                       ("primal_value", bound["primal_value"] + 1, "primal schedule audit mismatch"),
                       ("certified_bound", bound["certified_bound"] + 1, "must equal its audited primal"))
        for key, replacement, message in alterations:
            changed = dict(bound, **{key: replacement})
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, message):
                cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, certified_bounds=[changed])
        # Incumbent hints are not part of the fixed optimisation problem.
        hinted = copy.deepcopy(scene)
        hinted["baselines"] = [{"policy": "fcfs", "dispatches": []}]
        self.assertEqual(cp.scenario_sha256(scene), cp.scenario_sha256(hinted))

    def test_feasible_component_bounds_round_outward_and_reuse_one_sided(self):
        from ortools.sat.python import cp_model
        real_solver = cp_model.CpSolver
        scene = fixture([job("long", pages=10), job("short", release=2)],
                        [[10], [1]], horizon=20)
        records = []
        for component, adjustment, expected in (("sum_wait", -0.25, 2),
                                                 ("profit", 0.25, 21)):
            # A deterministic wrapper exercises the FEASIBLE reporting path
            # without an unreliable millisecond time limit.  Its dual bound is
            # deliberately weaker than the true optimum and remains valid.
            class FeasibleSolver(real_solver):
                def solve(self, model, callback=None):
                    status = super().solve(model, callback)
                    return cp_model.FEASIBLE if status == cp_model.OPTIMAL else status

                @property
                def best_objective_bound(self):
                    return super().best_objective_bound + adjustment

            with self.subTest(component=component), mock.patch.object(cp_model, "CpSolver", FeasibleSolver):
                diagnostic = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1,
                                                  objective_component=component)
            bound = diagnostic["component_bound"]
            self.assertEqual(diagnostic["status"], "FEASIBLE")
            self.assertEqual(diagnostic["proof"]["termination"], "component_time_limit")
            self.assertEqual(bound["solver_status"], "FEASIBLE")
            self.assertEqual(bound["certified_bound"], expected)
            if component == "sum_wait":
                self.assertLessEqual(bound["certified_bound"], bound["primal_value"])
            else:
                self.assertGreaterEqual(bound["certified_bound"], bound["primal_value"])
            records.append(bound)
        result = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, certified_bounds=records)
        self.assertEqual(result["status"], "OPTIMAL", result["proof"])
        self.assertEqual(result["proof"]["certified_component_bounds"], records)
        self.assertEqual(Fraction(result["quality"]["quality_score_fraction"]),
                         Fraction(exact.solve_scenario(scene)["quality"]["quality_score_fraction"]))
        # An alleged lower bound above its own feasible primal, or an upper
        # bound below it, cannot be applied even with FEASIBLE status.
        for bound in records:
            changed = dict(bound)
            changed["certified_bound"] = bound["primal_value"] + (1 if bound["sense"] == "min" else -1)
            with self.subTest(component=bound["objective_component"]), self.assertRaisesRegex(ValueError, "component bound"):
                cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, certified_bounds=[changed])

    def test_component_record_extraction_and_diagnostics_do_not_certify_q(self):
        scene = fixture([job("a"), job("b", release=2)], [[7], [1]], horizon=15)
        report = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, objective_component="sum_wait")
        record = report["component_bound"]
        self.assertEqual(cp.component_bounds_from_report(record), [record])
        self.assertEqual(cp.component_bounds_from_report(report), [record])
        self.assertEqual(cp.component_bounds_from_report({"scenarios": [report]}), [record])
        self.assertEqual(cp.component_bounds_from_report({"component_bounds": [record]}), [record])
        self.assertEqual(cp.component_bounds_from_report([report, record]), [record, record])
        with self.assertRaisesRegex(ValueError, "no component bound"):
            cp.component_bounds_from_report({"kind": "unrelated"})
        linear = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, linear_components=True)
        self.assertEqual(linear["proof"]["phase_one_status"], "OPTIMAL")
        self.assertEqual(linear["status"], "FEASIBLE")
        self.assertFalse(linear["proof"]["complete"])
        self.assertIsNone(linear["proof"]["phase_two_status"])
        self.assertNotIn("component_bound", linear)
        self.assertEqual(linear["proof"]["termination"], "linear_diagnostic_complete")
        self.assertGreaterEqual(Fraction(linear["proof"]["upper_bound_fraction"]),
                                Fraction(exact.solve_scenario(scene)["quality"]["quality_score_fraction"]))
        tangent = cp.solve_scenario_cp(scene, time_limit_s=1, workers=1, objective_component="tangent")
        self.assertEqual(tangent["status"], "FEASIBLE")
        self.assertFalse(tangent["proof"]["complete"])
        self.assertIn("tangent_warmstart", tangent["proof"])
        self.assertNotIn("component_bound", tangent)
        self.assertEqual(tangent["quality"], exact.evaluate(scene, tangent["dispatches"]))


class CppStressReplayTest(unittest.TestCase):
    def runCore(self, document):
        if not REPLAY:
            self.skipTest("pass --replay to audit the C++ production baseline tool")
        with tempfile.TemporaryDirectory(prefix="CP stress C++ ") as directory:
            source, output = Path(directory) / "input.json", Path(directory) / "audit.json"
            source.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
            run = subprocess.run([REPLAY, "--input", str(source), "--output", str(output)],
                                 capture_output=True, timeout=20)
            audit = json.loads(output.read_text(encoding="utf-8")) if output.exists() else None
            return run, audit

    def test_all_100_production_prices_matrices_and_baseline_traces(self):
        document = stress.generate_document()
        run, audited = self.runCore(document)
        self.assertEqual(run.returncode, 0, run.stderr.decode("utf-8", errors="replace"))
        self.assertEqual(audited["kind"], "campus-print-stress-baselines")
        expected = {b["policy"]: b for b in document["scenarios"][0]["baselines"]}
        self.assertEqual({r["policy"] for r in audited["runs"]}, set(expected))
        for replay in audited["runs"]:
            baseline = expected[replay["policy"]]
            self.assertEqual(replay["dispatches"], baseline["dispatches"])
            self.assertEqual(len(replay["jobs"]), 100)
            for key, value in replay["quality"].items():
                self.assertAlmostEqual(value, baseline["quality"][key], places=8)

    def test_cpp_rejects_mismatching_quote_duration_and_cost(self):
        original = stress.generate_document()
        for field in ("quoted_cents", "processing_ms", "cost_cents"):
            document = copy.deepcopy(original)
            scene = document["scenarios"][0]
            if field == "quoted_cents":
                scene["jobs"][0][field] += 1
            else:
                column = next(i for i, value in enumerate(scene[field][0]) if value is not None)
                scene[field][0][column] += 1
            with self.subTest(field=field):
                run, audited = self.runCore(document)
                self.assertEqual(run.returncode, 64)
                self.assertIsNone(audited)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--replay")
    options, remaining = parser.parse_known_args()
    REPLAY = options.replay
    unittest.main(argv=[sys.argv[0], *remaining])
