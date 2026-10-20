"""Prove the best fixed-cohort print schedule by exact rational enumeration.

Input is produced by teaching-schedule-input. This is an offline reference
solver, not a policy for an open-ended stream of previously unknown orders.
"""
import argparse
from dataclasses import dataclass
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys

REFERENCE_MS = 1800000
MAX_TIME_MS = 9007199254740991
OBJECTIVE = 'balanced-shop-quality-v1'


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _integer(value, label, minimum=0, maximum=MAX_TIME_MS):
    _require(type(value) is int and minimum <= value <= maximum,
             f'{label} must be an integer in [{minimum}, {maximum}]')
    return value


@dataclass
class _Problem:
    source: dict
    jobs: list
    printers: list
    duration: list
    cost: list
    horizon: int
    stock: tuple
    paper: list
    potential: int
    express: int


def _prepare(scenario):
    horizon = _integer(scenario['horizon_ms'], 'horizon_ms', 1)
    jobs, printers = scenario['jobs'], scenario['printers']
    _require(isinstance(jobs, list) and isinstance(printers, list), 'jobs/printers must be lists')
    _require(1 <= len(printers) <= 64, 'expected 1..64 printers')
    duration, cost = scenario['processing_ms'], scenario['cost_cents']
    _require(len(duration) == len(cost) == len(jobs), 'matrix row count differs from jobs')
    stock = tuple(_integer(scenario['inventory'][p], f'inventory.{p}') for p in ('a4', 'a3'))
    ids, paper = set(), []
    for p in printers:
        for key in ('online', 'a3', 'color', 'duplex'):
            _require(type(p[key]) is bool, f'printer.{key} must be boolean')
    for i, job in enumerate(jobs):
        ident = job['job_id']
        _require(isinstance(ident, str) and ident and ident not in ids, 'empty or duplicate job_id')
        ids.add(ident)
        pages = _integer(job['pages'], 'pages', 1, 1000)
        copies = _integer(job['copies'], 'copies', 1, 100)
        _require(pages * copies <= 10000, 'too many printed faces')
        _integer(job['submitted_ms'], 'submitted_ms', 0, horizon)
        deadline = _integer(job['deadline_ms'], 'deadline_ms')
        _require(deadline == 0 or deadline >= job['submitted_ms'], 'deadline precedes arrival')
        _require(job['paper'] in ('a4', 'a3'), 'unknown paper size')
        _require(job['service'] in ('express', 'standard', 'flexible'), 'unknown service tier')
        for key in ('color', 'duplex'):
            _require(type(job[key]) is bool, f'job.{key} must be boolean')
        quote = _integer(job['quoted_cents'], 'quoted_cents', 0, 3000000)
        refund = _integer(job.get('refund_cents', 0), 'refund_cents', 0, quote)
        _require(refund == 0, 'only fresh orders without refunds are supported')
        _integer(job['nominal_cost_cents'], 'nominal_cost_cents', 0, 3000000)
        sheets = ((pages + 1) // 2 if job['duplex'] else pages) * copies
        paper.append((int(job['paper'] == 'a3'), sheets))
        _require(len(duration[i]) == len(cost[i]) == len(printers), 'matrix column count differs from fleet')
        for m, printer in enumerate(printers):
            compatible = (printer['online'] and (job['paper'] != 'a3' or printer['a3']) and
                          (not job['color'] or printer['color']) and
                          (not job['duplex'] or printer['duplex']))
            if not compatible:
                _require(duration[i][m] is None and cost[i][m] is None,
                         'incompatible/offline device must have null duration and cost')
                continue
            _integer(duration[i][m], 'processing_ms', 1)
            direct = _integer(cost[i][m], 'cost_cents', 0, 3000000)
            _require(quote - refund - direct >= 0,
                     'negative contribution is unsupported by the earliest-start proof')
    return _Problem(scenario, jobs, printers, duration, cost, horizon, stock, paper,
                    sum(max(0, j['quoted_cents'] - j['nominal_cost_cents']) for j in jobs),
                    sum(j['service'] == 'express' for j in jobs))


def _score(problem, trace):
    """All score comparisons use Fraction, including the nearest-rank P95."""
    n = len(problem.jobs)
    if not n:
        return Fraction(0), ([], 0, 0, 0)
    waits = [problem.horizon - j['submitted_ms'] for j in problem.jobs]
    timely = contribution = revenue = 0
    for i, m, start, end in trace:
        job = problem.jobs[i]
        waits[i] = start - job['submitted_ms']
        if end <= problem.horizon:
            paid = job['quoted_cents'] - job.get('refund_cents', 0)
            revenue += paid
            contribution += paid - problem.cost[i][m]
            timely += (job['service'] == 'express' and job['deadline_ms'] > 0 and
                       end <= job['deadline_ms'])
    p95 = sorted(waits)[(95 * n + 99) // 100 - 1]
    waiting = (Fraction(n * REFERENCE_MS, n * REFERENCE_MS + sum(waits)) +
               Fraction(REFERENCE_MS, REFERENCE_MS + p95)) / 2
    express = Fraction(timely, problem.express) if problem.express else Fraction(1)
    profit = (min(Fraction(1), max(Fraction(0), Fraction(contribution, problem.potential)))
              if problem.potential else Fraction(0))
    return 100 * (waiting + express + profit) / 3, (waits, timely, contribution, revenue)


def _quality(problem, trace):
    score, (waits, timely, contribution, revenue) = _score(problem, trace)
    n = len(waits)
    mean = Fraction(sum(waits), n) if n else Fraction(0)
    p95 = sorted(waits)[(95 * n + 99) // 100 - 1] if n else 0
    waiting = ((Fraction(REFERENCE_MS, REFERENCE_MS + mean) +
                Fraction(REFERENCE_MS, REFERENCE_MS + p95)) / 2 if n else Fraction(0))
    express = (Fraction(timely, problem.express) if problem.express else Fraction(bool(n)))
    profit = (min(Fraction(1), max(Fraction(0), Fraction(contribution, problem.potential)))
              if problem.potential else Fraction(0))
    return {
        'average_observed_wait_ms': float(mean), 'average_observed_wait_ms_fraction': str(mean),
        'p95_observed_wait_ms': p95, 'express_accepted': problem.express,
        'express_on_time': timely, 'express_on_time_rate': float(express),
        'potential_contribution_cents': problem.potential,
        'contribution_cents': contribution, 'revenue_cents': revenue,
        'completed': sum(end <= problem.horizon for _, _, _, end in trace),
        'waiting_score': float(waiting), 'contribution_score': float(profit),
        'overall_score': float(score), 'quality_score_fraction': str(score),
    }


def _validated_trace(problem, dispatches):
    indices = {j['job_id']: i for i, j in enumerate(problem.jobs)}
    seen, trace = set(), []
    used_paper = [0, 0]
    for dispatch in dispatches:
        ident = dispatch['job_id']
        _require(ident in indices and ident not in seen, 'unknown or duplicate dispatch')
        seen.add(ident)
        i = indices[ident]
        m = _integer(dispatch['printer'], 'printer', 0, len(problem.printers) - 1)
        start = _integer(dispatch['started_ms'], 'started_ms', 0, problem.horizon - 1)
        end = _integer(dispatch['finished_ms'], 'finished_ms', 1)
        _require(problem.duration[i][m] is not None, 'incompatible device')
        _require(start >= problem.jobs[i]['submitted_ms'], 'dispatch precedes arrival')
        _require(end - start == problem.duration[i][m], 'wrong processing duration')
        kind, sheets = problem.paper[i]
        used_paper[kind] += sheets
        trace.append((i, m, start, end))
    _require(all(a <= b for a, b in zip(used_paper, problem.stock)), 'insufficient paper')
    for m in range(len(problem.printers)):
        end = 0
        for _, _, start, finish in sorted((d for d in trace if d[1] == m), key=lambda d: d[2]):
            _require(start >= end, 'overlapping printer intervals')
            end = finish
    return sorted(trace, key=lambda d: (d[2], d[1], d[0]))


def evaluate(scenario, dispatches):
    """Validate a schedule and independently reconstruct its objective fields."""
    problem = _prepare(scenario)
    return _quality(problem, _validated_trace(problem, dispatches))


def solve_scenario(scenario, max_nodes=1000000):
    """Enumerate selected subsets, per-printer orders and assignments exactly.

    For fixed per-printer sequences, left shifting to max(release, predecessor
    finish) cannot hurt the objective when completed contributions are >= 0.
    Every subset is considered, including schedules with work crossing closing.
    Appends are canonically sorted by (start, printer, input index), so different
    cross-printer insertion orders do not multiply identical schedules.
    """
    problem = _prepare(scenario)
    _require(len(problem.jobs) <= 10, 'exact enumeration supports at most 10 orders')
    _integer(max_nodes, 'max_nodes', 1)
    best_score, _ = _score(problem, [])
    best_trace, trace = [], []
    free, used_paper = [0] * len(problem.printers), [0, 0]
    visited = optima = 0
    complete = True

    def visit(mask, last_key):
        nonlocal best_score, best_trace, visited, complete, optima
        if visited >= max_nodes:
            complete = False
            return
        visited += 1
        score, _ = _score(problem, trace)
        if score > best_score:
            best_score, best_trace, optima = score, list(trace), 1
        elif score == best_score:
            optima += 1
        for i, job in enumerate(problem.jobs):
            if mask & (1 << i):
                continue
            kind, sheets = problem.paper[i]
            if used_paper[kind] + sheets > problem.stock[kind]:
                continue
            for m in range(len(problem.printers)):
                duration = problem.duration[i][m]
                if duration is None:
                    continue
                start = max(job['submitted_ms'], free[m])
                key = (start, m, i)
                if start >= problem.horizon or key < last_key or start + duration > MAX_TIME_MS:
                    continue
                old_free = free[m]
                free[m] = start + duration
                used_paper[kind] += sheets
                trace.append((i, m, start, free[m]))
                visit(mask | (1 << i), key)
                trace.pop()
                used_paper[kind] -= sheets
                free[m] = old_free
                if not complete:
                    return

    visit(0, (-1, -1, -1))
    dispatches = [{'job_id': problem.jobs[i]['job_id'], 'printer': m,
                   'started_ms': start, 'finished_ms': end} for i, m, start, end in best_trace]
    quality = evaluate(scenario, dispatches)
    upper = best_score if complete else Fraction(100 if problem.jobs else 0)
    return {
        'name': scenario.get('name', ''), 'status': 'OPTIMAL' if complete else 'FEASIBLE',
        'time_unit': scenario.get('time_unit', 'ms'), 'horizon_ms': problem.horizon,
        'dispatches': dispatches, 'quality': quality,
        'proof': {'method': 'exhaustive_earliest_start_sequences_exact_rationals',
                  'complete': complete, 'visited_nodes': visited, 'max_nodes': max_nodes,
                  'canonical_optima_count': optima if complete else None,
                  'lower_bound_fraction': str(best_score), 'upper_bound_fraction': str(upper),
                  'absolute_gap_fraction': str(upper - best_score),
                  'termination': 'exhausted' if complete else 'max_nodes'},
    }


def solve_document(document, max_nodes=1000000):
    _require(document.get('kind') == 'campus-print-optimal-input' and
             document.get('schema_version') == 1, 'unsupported input format')
    objective = document['objective']
    _require(objective['name'] == OBJECTIVE and objective['wait_reference_ms'] == REFERENCE_MS and
             all(objective[k] == 1 for k in ('waiting_weight', 'express_weight', 'contribution_weight')),
             'objective must remain the fixed equal-weight production objective')
    results = []
    for scenario in document['scenarios']:
        result = solve_scenario(scenario, max_nodes)
        baselines = []
        for baseline in scenario.get('baselines', []):
            quality = evaluate(scenario, baseline['dispatches'])
            for key, value in baseline.get('quality', {}).items():
                if key in quality:
                    _require(abs(quality[key] - value) <= 1e-8 * max(1, abs(value)),
                             f'C++ baseline audit mismatch: {baseline["policy"]}.{key}')
            delta = Fraction(result['quality']['quality_score_fraction']) - Fraction(quality['quality_score_fraction'])
            _require(result['status'] != 'OPTIMAL' or delta >= 0, 'optimum is below a feasible baseline')
            baselines.append({'policy': baseline['policy'], 'quality': quality,
                              'optimal_improvement_fraction': str(delta),
                              'dispatches': baseline['dispatches']})
        result['baselines'] = baselines
        results.append(result)
    return {'schema_version': 1, 'kind': 'campus-print-optimal-result', 'objective': objective,
            'scope': 'fixed known fresh orders; no failures, restocking or unknown future arrivals; intentional idling allowed',
            'scenarios': results}


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--max-nodes', type=int, default=1000000)
    args = parser.parse_args(argv)
    try:
        _require(args.input.resolve() != args.output.resolve(), 'input and output must differ')
        _require(not args.output.exists(), 'output must be a new file')
        raw = args.input.read_bytes()
        document = json.loads(raw.decode('utf-8-sig'))
        report = solve_document(document, args.max_nodes)
        report['evidence'] = {'input_sha256': hashlib.sha256(raw).hexdigest(),
                              'solver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                              'python_version': sys.version.split()[0]}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        for result in report['scenarios']:
            sequence = ' -> '.join(d['job_id'] for d in result['dispatches'])
            print(f'{result["name"]}: {result["status"]}, score={result["quality"]["overall_score"]:.10f}, {sequence}')
        return 0 if all(r['status'] == 'OPTIMAL' for r in report['scenarios']) else 2
    except (KeyError, TypeError, ValueError, OSError) as error:
        print(f'optimal schedule: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
