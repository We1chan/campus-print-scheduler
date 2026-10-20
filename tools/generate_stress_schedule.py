"""Reproducible 100-order overload fixture and production-policy replays.

The fixture stresses several independent resource constraints.  It is not a
claim that a unique mathematically 'most complex' scheduling instance exists.
Pricing, paper rounding, fleet speeds and replay ties mirror core/shop.cpp and
core/scheduler.cpp.  No optimisation result is used when generating the input.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import random
import sys


SEED = 20261020
HORIZON_MS = 2 * 60 * 60 * 1000
WINDOW_MS = {"express": 30 * 60 * 1000, "standard": 120 * 60 * 1000,
             "flexible": 480 * 60 * 1000}


def default_fleet(base=1250):
    """The six production defaultFleet printers, in production index order."""
    base = max(10, min(base, 1250))
    data = [("黑白快印 01", False, False, True, base, base),
            ("黑白快印 02", False, False, True, base * 2, base * 2),
            ("黑白单面 03", False, False, False, base * 3, base * 3),
            ("彩色双面 04", False, True, True, base * 2, base * 3),
            ("彩色快印 05", False, True, True, base, base * 2),
            ("A3 全能 06", True, True, True, base * 2, base * 4)]
    return [{"name": name, "online": True, "a3": a3, "color": color,
             "duplex": duplex, "mono_page_ms": mono, "color_page_ms": colour}
            for name, a3, color, duplex, mono, colour in data]


def paper_sheets(job):
    return ((job["pages"] + 1) // 2 if job["duplex"] else job["pages"]) * job["copies"]


def compatible(job, printer):
    return (printer["online"] and (job["paper"] != "a3" or printer["a3"]) and
            (not job["color"] or printer["color"]) and
            (not job["duplex"] or printer["duplex"]))


def quote_job(job):
    """Apply production integer-cent discounts and standard delivery promises."""
    faces = job["pages"] * job["copies"]
    job["base_cents"] = faces * (150 if job["color"] else 20) * (2 if job["paper"] == "a3" else 1)
    discount = {"express": 100, "standard": 95, "flexible": 85}[job["service"]]
    amount = (job["base_cents"] * discount + 50) // 100
    job["quoted_cents"] = (amount * 95 + 50) // 100 if job["duplex"] else amount
    job["deadline_ms"] = job["submitted_ms"] + WINDOW_MS[job["service"]]
    job["refund_cents"] = 0
    job["nominal_cost_cents"] = direct_cost(job, {"color": False})
    return job


def direct_cost(job, printer):
    multiplier = 2 if job["paper"] == "a3" else 1
    ink = 45 if job["color"] else 7 if printer["color"] else 5
    return paper_sheets(job) * (6 if job["paper"] == "a3" else 3) + job["pages"] * job["copies"] * ink * multiplier


def processing_ms(job, printer):
    per_face = printer["color_page_ms"] if job["color"] else printer["mono_page_ms"]
    return per_face * job["pages"] * job["copies"] * (2 if job["paper"] == "a3" else 1)


def replay_fcfs_sjf(scenario, policy="fcfs"):
    """Return dispatches from an event replay of FCFS or SJF, without lookahead.

    Stock is permanently reserved at start, including jobs finishing after H.
    Reconsider the queue after each dispatch, as Scheduler::claimFor does.
    """
    if policy not in ("fcfs", "sjf"):
        raise ValueError("policy must be fcfs or sjf")
    jobs, fleet = scenario["jobs"], scenario["printers"]
    duration, costs = scenario["processing_ms"], scenario["cost_cents"]
    horizon = scenario["horizon_ms"]
    stock = dict(scenario["inventory"])
    queued = set(range(len(jobs)))
    free_at = [0] * len(fleet)
    dispatches, now = [], 0
    while now < horizon:
        idle = {m for m, printer in enumerate(fleet) if printer["online"] and free_at[m] <= now}
        while idle:
            waiting = [i for i in queued if jobs[i]["submitted_ms"] <= now and
                       paper_sheets(jobs[i]) <= stock[jobs[i]["paper"]]]
            if policy == "fcfs":
                waiting.sort(key=lambda i: (jobs[i]["submitted_ms"], i))
            else:
                waiting.sort(key=lambda i: (jobs[i]["pages"] * jobs[i]["copies"], jobs[i]["submitted_ms"], i))
            chosen = None
            for i in waiting:
                available = [m for m in idle if duration[i][m] is not None]
                if available:
                    m = min(available) if policy == "fcfs" else min(available, key=lambda m: (duration[i][m], costs[i][m], m))
                    chosen = i, m
                    break
            if chosen is None:
                break
            i, m = chosen
            queued.remove(i)
            stock[jobs[i]["paper"]] -= paper_sheets(jobs[i])
            free_at[m] = now + duration[i][m]
            idle.remove(m)
            dispatches.append({"job_id": jobs[i]["job_id"], "printer": m,
                               "started_ms": now, "finished_ms": free_at[m]})
        future = [horizon]
        future.extend(t for t in free_at if t > now)
        future.extend(jobs[i]["submitted_ms"] for i in queued if jobs[i]["submitted_ms"] > now)
        now = min(future)
    return dispatches


def workload_summary(scenario):
    jobs, horizon = scenario["jobs"], scenario["horizon_ms"]
    demand = {paper: sum(paper_sheets(job) for job in jobs if job["paper"] == paper)
              for paper in ("a4", "a3")}
    fastest = sum(min(t for t in row if t is not None) for row in scenario["processing_ms"])
    unique = [i for i, row in enumerate(scenario["processing_ms"]) if sum(t is not None for t in row) == 1]
    return {"orders": len(jobs), "printers": len(scenario["printers"]),
            "horizon_ms": horizon, "total_faces": sum(j["pages"] * j["copies"] for j in jobs),
            "paper_demand_sheets": demand, "inventory_sheets": dict(scenario["inventory"]),
            "paper_demand_stock_ratio": {p: demand[p] / scenario["inventory"][p] for p in demand},
            "fastest_total_work_ms": fastest,
            "fastest_work_fleet_horizon_ratio": fastest / (len(scenario["printers"]) * horizon),
            "a3_single_printer_work_horizon_ratio": sum(scenario["processing_ms"][i][5] for i in unique) / horizon,
            "single_compatible_printer_orders": len(unique),
            "color_orders": sum(j["color"] for j in jobs),
            "duplex_orders": sum(j["duplex"] for j in jobs),
            "tiers": dict(sorted(Counter(j["service"] for j in jobs).items())),
            "min_faces": min(j["pages"] * j["copies"] for j in jobs),
            "max_faces": max(j["pages"] * j["copies"] for j in jobs),
            "arrival_timestamps": len({j["submitted_ms"] for j in jobs}),
            "arrived_at_zero": sum(j["submitted_ms"] == 0 for j in jobs)}


def generate_scenario(seed=SEED):
    rng = random.Random(seed)
    # Fixed mixture chosen before solving: 35 short, 40 medium, 20 bulk, 5 very
    # large jobs.  No rejection/resampling based on optimised scores or proof time.
    sizes = ["short"] * 35 + ["medium"] * 40 + ["bulk"] * 20 + ["large"] * 5
    rng.shuffle(sizes)
    releases = [0] * 35 + [5 * 60000] * 20 + [15 * 60000] * 20 + [30 * 60000] * 15 + [50 * 60000] * 10
    rng.shuffle(releases)
    jobs = []
    for i, size in enumerate(sizes):
        if size == "short":
            pages, copies = rng.randint(1, 24), rng.randint(1, 3)
        elif size == "medium":
            pages, copies = rng.randint(25, 120), rng.randint(2, 8)
        elif size == "bulk":
            pages, copies = rng.randint(80, 250), rng.randint(4, 12)
        else:
            pages, copies = rng.randint(400, 900), rng.randint(6, 10)
        release = releases[i] + (rng.choice((0, 0, 15000, 30000)) if releases[i] else 0)
        job = {"job_id": f"STRESS-{i + 1:03d}", "title": f"压力订单 {i + 1:03d}",
               "pages": pages, "copies": copies, "submitted_ms": release,
               "paper": "a3" if rng.random() < 0.22 else "a4", "color": rng.random() < 0.42,
               "duplex": rng.random() < 0.65,
               "service": rng.choices(("express", "standard", "flexible"), (45, 35, 20))[0]}
        jobs.append(quote_job(job))
    fleet = default_fleet()
    scenario = {"name": "100 单异构设备与纸张双瓶颈压力场景", "time_unit": "ms",
                "horizon_ms": HORIZON_MS, "jobs": jobs, "printers": fleet,
                "inventory": {"a4": 10000, "a3": 3000},
                "processing_ms": [[processing_ms(job, printer) if compatible(job, printer) else None for printer in fleet] for job in jobs],
                "cost_cents": [[direct_cost(job, printer) if compatible(job, printer) else None for printer in fleet] for job in jobs],
                "generation": {"generator": "stress-100-v1", "seed": seed,
                               "maximum_complexity_claim": False,
                               "description": "固定混合分布和种子；突发到达、异构能力、长短单、紧迫期限及纸张不足；未按求解结果挑选输入。"}}
    scenario["workload_summary"] = workload_summary(scenario)
    from solve_optimal_schedule import evaluate
    scenario["baselines"] = []
    for policy in ("fcfs", "sjf"):
        dispatches = replay_fcfs_sjf(scenario, policy)
        scenario["baselines"].append({"policy": policy, "dispatches": dispatches,
                                      "quality": evaluate(scenario, dispatches)})
    return scenario


def generate_document(seed=SEED):
    return {"schema_version": 1, "kind": "campus-print-optimal-input",
            "objective": {"name": "balanced-shop-quality-v1", "wait_reference_ms": 1800000,
                          "waiting_weight": 1, "express_weight": 1, "contribution_weight": 1},
            "scenarios": [generate_scenario(seed)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Write a new UTF-8 JSON file; existing files are never overwritten")
    parser.add_argument("--seed", type=int, default=SEED)
    args = parser.parse_args()
    document = generate_document(args.seed)
    data = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(data)
        print(json.dumps(document["scenarios"][0]["workload_summary"], ensure_ascii=False, indent=2))
    else:
        sys.stdout.write(data)


if __name__ == "__main__":
    main()
