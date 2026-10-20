"""Bound and certify fixed-cohort schedules without changing their rational score.

CP-SAT first maximizes four scaled integer floors.  All aggregate tuples which
could improve the incumbent's exact Fraction score are then exhausted.  Only an
INFEASIBLE result from that second search certifies the original objective.
"""
import argparse
from fractions import Fraction
from functools import reduce
import hashlib
import json
from math import ceil, floor, gcd
from pathlib import Path
import sys
import time

from solve_optimal_schedule import (MAX_TIME_MS, OBJECTIVE, REFERENCE_MS, _prepare, _require,
                                    evaluate)

SCALE = 1_000_000_000
COMPONENTS = {'timely': ('max', 'count'), 'profit': ('max', 'cents'),
              'sum_wait': ('min', 'ticks'), 'p95': ('min', 'ticks')}


def scenario_sha256(scenario):
    """Fingerprint the fixed input independently of optional incumbent hints."""
    source = {key: value for key, value in scenario.items() if key != 'baselines'}
    return hashlib.sha256(json.dumps(source, sort_keys=True, ensure_ascii=False,
                                     separators=(',', ':')).encode('utf-8')).hexdigest()


def component_bounds_from_report(value):
    """Extract reusable bound records from solver reports or a record list."""
    if isinstance(value, list):
        return [bound for item in value for bound in component_bounds_from_report(item)]
    if value.get('kind') == 'campus-print-component-bound':
        return [value]
    if value.get('component_bound') is not None:
        return [value['component_bound']]
    if 'scenarios' in value:
        return component_bounds_from_report(value['scenarios'])
    if 'component_bounds' in value:
        return component_bounds_from_report(value['component_bounds'])
    raise ValueError('file contains no component bound records')


def _add_reciprocal_chords(model, argument, reward, numerator, offset, maximum,
                           name, segments=32):
    """Strengthen integer division with valid outward-rounded chord bounds.

    On [l,u], f(x)=numerator/(offset+x) is convex and hence below
    f(l)-numerator*(x-l)/((offset+l)*(offset+u)).  Rounding the
    intercept upwards and the nonnegative slope downwards preserves this upper
    bound.  Small coefficients avoid the products of large denominators.
    """
    if maximum == 0:
        return
    count = min(segments, maximum + 1)
    selectors = []
    coefficient_scale = 1000
    global_intercept = (coefficient_scale * numerator + offset - 1) // offset
    global_slope = coefficient_scale * numerator // (offset * (offset + maximum))
    model.add(coefficient_scale * reward <= global_intercept - global_slope * argument)
    for index in range(count):
        low = (maximum + 1) * index // count
        high = (maximum + 1) * (index + 1) // count - 1
        selected = model.new_bool_var(f'{name}_segment_{index}')
        selectors.append(selected)
        model.add(argument >= low).only_enforce_if(selected)
        model.add(argument <= high).only_enforce_if(selected)
        left_denominator = offset + low
        denominator_product = left_denominator * (offset + high)
        intercept = (coefficient_scale * numerator + left_denominator - 1) // left_denominator
        slope = coefficient_scale * numerator // denominator_product
        model.add(coefficient_scale * reward <= intercept - slope * (argument - low)).only_enforce_if(selected)
    model.add_exactly_one(selectors)


def _write_progress(path, report):
    if path is None:
        return
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n',
                         encoding='utf-8')
    temporary.replace(path)


def _fraction_score(quality):
    return Fraction(quality['quality_score_fraction'])


def _scaled_objective(problem, aggregate, tick):
    total_wait, p95, timely, contribution = aggregate
    n = len(problem.jobs)
    reference = REFERENCE_MS // tick
    return (SCALE * n * reference // (n * reference + total_wait) +
            SCALE * reference // (reference + p95) +
            (2 * SCALE * timely // problem.express if problem.express else 2 * SCALE) +
            (2 * SCALE * contribution // problem.potential if problem.potential else 0))


def solve_scenario_cp(scenario, time_limit_s=300.0, workers=8, seed=20261020,
                      progress_path=None, linear_components=False,
                      linear_chords=True, resource_cuts=True,
                      objective_component=None, certified_bounds=None):
    """Return an auditable incumbent and a rigorous bound under an overall budget.

    ``linear_components`` is a diagnostic objective, never an OPTIMAL certificate
    for the production score.  It helps distinguish scheduling-model difficulty
    from integer-division propagation difficulty.
    """
    from ortools.sat.python import cp_model

    started = time.monotonic()
    _require(time_limit_s > 0 and workers >= 1, 'positive time budget/workers required')
    _require(objective_component is None or objective_component in COMPONENTS or objective_component == 'tangent',
             'unknown objective component')
    _require(not (objective_component and linear_components), 'choose one diagnostic objective')
    problem = _prepare(scenario)
    jobs, n = problem.jobs, len(problem.jobs)
    best_dispatches = []
    best_quality = evaluate(scenario, [])
    audited_baselines = []
    for baseline in scenario.get('baselines', []):
        quality = evaluate(scenario, baseline['dispatches'])
        audited_baselines.append({'policy': baseline['policy'], 'quality': quality,
                                  'dispatches': baseline['dispatches']})
        if _fraction_score(quality) > _fraction_score(best_quality):
            best_dispatches = list(baseline['dispatches'])
            best_quality = quality
    initial_score = _fraction_score(best_quality)

    times = [REFERENCE_MS, problem.horizon]
    times.extend(j['submitted_ms'] for j in jobs)
    times.extend(j['deadline_ms'] for j in jobs)
    times.extend(p for row in problem.duration for p in row if p is not None)
    tick = reduce(gcd, times)
    horizon, reference = problem.horizon // tick, REFERENCE_MS // tick
    fingerprint = scenario_sha256(scenario)
    component_bound = None
    proof = {'method': 'cp_sat_scaled_floor_then_exact_aggregate_exhaustion',
             'complete': False, 'scale': SCALE, 'tick_ms': tick,
             'time_limit_s': time_limit_s, 'workers': workers, 'seed': seed,
             'solver_version': __import__('ortools').__version__,
             'phase_one_status': None, 'phase_two_status': None,
             'scaled_objective_optimal': None, 'aggregate_tuples': [],
             'objective_floor_error_strictly_less_than': 4,
             'scaled_objective_identity': 'U <= 6*scale*Q/100 < U+4',
             'dominance_cuts': 'exclude A>=known_A, B>=known_B, T<=known_T, G<=known_G',
             'diagnostic_linear_components': bool(linear_components)}
    proof['scenario_sha256'] = fingerprint
    proof['diagnostic_objective_component'] = objective_component
    proof['reciprocal_chord_segments'] = 32 if linear_chords and not linear_components and not objective_component else 0
    global_upper = Fraction(100 if n else 0)
    complete = not n
    termination = 'empty_cohort' if not n else 'time_limit'

    def report():
        lower = _fraction_score(best_quality)
        upper = lower if complete else max(lower, global_upper)
        proof.update({'complete': complete, 'termination': termination,
                      'elapsed_seconds': time.monotonic() - started,
                      'lower_bound_fraction': str(lower),
                      'upper_bound_fraction': str(upper),
                      'absolute_gap_fraction': str(upper - lower),
                      'visited_nodes': sum(s.get('branches', 0) for s in proof.get('solves', []))})
        baselines = []
        for baseline in audited_baselines:
            baselines.append(dict(baseline, optimal_improvement_fraction=str(
                lower - _fraction_score(baseline['quality']))))
        result = {'name': scenario.get('name', ''),
                'status': 'OPTIMAL' if complete else 'FEASIBLE',
                'time_unit': scenario.get('time_unit', 'ms'),
                'horizon_ms': problem.horizon, 'dispatches': best_dispatches,
                'quality': best_quality, 'proof': dict(proof), 'baselines': baselines}
        if component_bound is not None:
            result['component_bound'] = component_bound
        return result

    if not n:
        result = report()
        _write_progress(progress_path, result)
        return result

    model = cp_model.CpModel()
    starts, active, assignments, waits = [], [], [], []
    intervals = [[] for _ in problem.printers]
    machine_resources = [[] for _ in problem.printers]
    completions, timely_bits, profit_terms = [], [], []
    for i, job in enumerate(jobs):
        release = job['submitted_ms'] // tick
        s = model.new_int_var(release, horizon, f'start_{i}')
        dispatched = model.new_bool_var(f'dispatched_{i}')
        model.add(s <= horizon - 1).only_enforce_if(dispatched)
        model.add(s == horizon).only_enforce_if(dispatched.Not())
        starts.append(s)
        active.append(dispatched)
        wait = model.new_int_var(0, horizon - release, f'wait_{i}')
        model.add(wait == s - release)
        waits.append(wait)
        choices = []
        for m in range(len(problem.printers)):
            if problem.duration[i][m] is None:
                assignments.append((i, m, None))
                continue
            duration = problem.duration[i][m] // tick
            x = model.new_bool_var(f'assign_{i}_{m}')
            assignments.append((i, m, x))
            choices.append(x)
            model.add(s + duration <= MAX_TIME_MS // tick).only_enforce_if(x)
            intervals[m].append(model.new_optional_fixed_size_interval_var(
                s, duration, x, f'interval_{i}_{m}'))
            done = model.new_bool_var(f'completed_{i}_{m}')
            model.add(done <= x)
            model.add(s + duration <= horizon).only_enforce_if(done)
            model.add(s + duration >= horizon + 1).only_enforce_if([x, done.Not()])
            completions.append(done)
            profit_terms.append((job['quoted_cents'] - problem.cost[i][m]) * done)
            ontime, cutoff = None, None
            if job['service'] == 'express' and job['deadline_ms'] > 0:
                ontime = model.new_bool_var(f'ontime_{i}_{m}')
                cutoff = min(horizon, job['deadline_ms'] // tick)
                model.add(ontime <= x)
                model.add(s + duration <= cutoff).only_enforce_if(ontime)
                model.add(s + duration >= cutoff + 1).only_enforce_if([x, ontime.Not()])
                timely_bits.append(ontime)
            machine_resources[m].append((release, duration, done, ontime, cutoff))
        model.add(sum(choices) == dispatched)
    for machine_intervals in intervals:
        model.add_no_overlap(machine_intervals)
    energy_count = 0
    if resource_cuts:
        # Every selected completed interval lies inside [release,H].  Every
        # selected timely interval additionally lies before its deadline.  These
        # necessary energy bounds expose NoOverlap capacity to the LP relaxation.
        for resources in machine_resources:
            releases = sorted({0} | {r for r, _, _, _, _ in resources if r < horizon})
            cutoffs = sorted({c for _, _, _, bit, c in resources if bit is not None})
            for low in releases:
                completed_energy = [duration * done for r, duration, done, _, _ in resources if r >= low]
                if completed_energy:
                    model.add(sum(completed_energy) <= horizon - low)
                    energy_count += 1
                for high in cutoffs:
                    if high < low:
                        continue
                    timely_energy = [duration * bit for r, duration, _, bit, cutoff in resources
                                     if bit is not None and r >= low and cutoff <= high]
                    if timely_energy:
                        model.add(sum(timely_energy) <= high - low)
                        energy_count += 1
    proof['resource_energy_cuts'] = energy_count
    for kind in (0, 1):
        model.add(sum(sheets * active[i] for i, (paper, sheets) in enumerate(problem.paper)
                      if paper == kind) <= problem.stock[kind])

    total_wait = model.new_int_var(0, n * horizon, 'sum_wait')
    model.add(total_wait == sum(waits))
    p95 = model.new_int_var(0, horizon, 'p95_wait')
    less, less_equal = [], []
    for i, wait in enumerate(waits):
        lt = model.new_bool_var(f'wait_lt_p95_{i}')
        le = model.new_bool_var(f'wait_le_p95_{i}')
        model.add(wait < p95).only_enforce_if(lt)
        model.add(wait >= p95).only_enforce_if(lt.Not())
        model.add(wait <= p95).only_enforce_if(le)
        model.add(wait > p95).only_enforce_if(le.Not())
        less.append(lt)
        less_equal.append(le)
    rank = (95 * n + 99) // 100
    model.add(sum(less_equal) >= rank)
    model.add(sum(less) <= rank - 1)
    timely = model.new_int_var(0, problem.express, 'express_on_time')
    model.add(timely == sum(timely_bits))
    max_profit = sum(max((j['quoted_cents'] - c for c in row if c is not None), default=0)
                     for j, row in zip(jobs, problem.cost))
    raw_profit = model.new_int_var(0, max_profit, 'raw_contribution')
    model.add(raw_profit == sum(profit_terms))
    contribution = model.new_int_var(0, problem.potential, 'capped_contribution')
    model.add_min_equality(contribution, [raw_profit, problem.potential])
    aggregates = [total_wait, p95, timely, contribution]
    component_variables = {'sum_wait': total_wait, 'p95': p95,
                           'timely': timely, 'profit': contribution}
    applied_bounds = []
    if certified_bounds:
        records = component_bounds_from_report(certified_bounds)
        matching = [b for b in records if b.get('scenario_sha256') == fingerprint]
        _require(bool(matching), 'component bound scenario hash mismatch')
        for bound in matching:
            _require(bound.get('schema_version') == 1 and
                     bound.get('kind') == 'campus-print-component-bound', 'unsupported component bound')
            _require(bound.get('scenario_sha256') == fingerprint, 'component bound scenario hash mismatch')
            component = bound.get('objective_component')
            _require(component in COMPONENTS and bound.get('sense') == COMPONENTS[component][0] and
                     bound.get('unit') == COMPONENTS[component][1], 'component bound sense/unit mismatch')
            _require(bound.get('tick_ms') == tick, 'component bound time unit mismatch')
            _require(bound.get('solver_status') in ('OPTIMAL', 'FEASIBLE'), 'component bound lacks a CP-SAT bound')
            value = bound.get('certified_bound')
            _require(type(value) is int and value >= 0, 'component bound must be a nonnegative integer')
            bound_quality = evaluate(scenario, bound.get('dispatches', []))
            observed_primal = {
                'timely': bound_quality['express_on_time'],
                'profit': min(problem.potential, bound_quality['contribution_cents']),
                'sum_wait': int(Fraction(bound_quality['average_observed_wait_ms_fraction']) * n // tick),
                'p95': bound_quality['p95_observed_wait_ms'] // tick,
            }[component]
            _require(bound.get('primal_value') == observed_primal, 'component bound primal schedule audit mismatch')
            _require(bound.get('solver_status') != 'OPTIMAL' or value == observed_primal,
                     'optimal component bound must equal its audited primal')
            if COMPONENTS[component][0] == 'max':
                _require(value >= observed_primal, 'upper component bound below its primal')
                model.add(component_variables[component] <= value)
            else:
                _require(value <= observed_primal, 'lower component bound above its primal')
                model.add(component_variables[component] >= value)
            applied_bounds.append(bound)
    proof['certified_component_bounds'] = applied_bounds
    if applied_bounds:
        lower_a = lower_b = 0
        upper_t, upper_g = problem.express, problem.potential
        for bound in applied_bounds:
            component, value = bound['objective_component'], bound['certified_bound']
            if component == 'sum_wait':
                lower_a = max(lower_a, value)
            elif component == 'p95':
                lower_b = max(lower_b, value)
            elif component == 'timely':
                upper_t = min(upper_t, value)
            else:
                upper_g = min(upper_g, value)
        relaxation = Fraction(100, 3) * (
            (Fraction(n * reference, n * reference + lower_a) +
             Fraction(reference, reference + lower_b)) / 2 +
            (Fraction(upper_t, problem.express) if problem.express else 1) +
            (Fraction(upper_g, problem.potential) if problem.potential else 0))
        global_upper = min(global_upper, relaxation)
        proof['component_relaxation_upper_bound_fraction'] = str(relaxation)

    if objective_component == 'tangent':
        point_a = Fraction(best_quality['average_observed_wait_ms_fraction']) * n / tick
        point_b = Fraction(best_quality['p95_observed_wait_ms'], tick)
        a = n * reference
        gradients = [Fraction(-a, 2) / (a + point_a) ** 2,
                     Fraction(-reference, 2) / (reference + point_b) ** 2,
                     Fraction(1, problem.express) if problem.express else Fraction(0),
                     Fraction(1, problem.potential) if problem.potential else Fraction(0)]
        coefficients = [round(SCALE * gradient) for gradient in gradients]
        objective = sum(coefficient * variable for coefficient, variable in zip(coefficients, aggregates))
        scaled = None
        proof['tangent_warmstart'] = {
            'point_sum_wait_ticks': str(point_a), 'point_p95_ticks': str(point_b),
            'gradient_fractions': [str(g) for g in gradients],
            'integer_coefficients': coefficients, 'coefficient_scale': SCALE,
            'initial_quality_fraction': str(initial_score),
            'scope': 'rounded linear proxy only; every incumbent audited with the original rational Q; no Q optimality claim'}
    elif objective_component:
        objective = component_variables[objective_component]
        scaled = None
    elif linear_components:
        objective = -total_wait - n * p95 + reference * timely + contribution
        scaled = None
    else:
        floor_mean = model.new_int_var(0, SCALE, 'floor_mean_reward')
        floor_p95 = model.new_int_var(0, SCALE, 'floor_p95_reward')
        floor_express = model.new_int_var(0, 2 * SCALE, 'floor_express_reward')
        floor_profit = model.new_int_var(0, 2 * SCALE, 'floor_contribution_reward')
        model.add_division_equality(floor_mean, SCALE * n * reference,
                                    n * reference + total_wait)
        model.add_division_equality(floor_p95, SCALE * reference, reference + p95)
        if linear_chords:
            max_waits = [horizon - job['submitted_ms'] // tick for job in jobs]
            _add_reciprocal_chords(model, total_wait, floor_mean, SCALE * n * reference,
                                   n * reference, sum(max_waits), 'mean')
            _add_reciprocal_chords(model, p95, floor_p95, SCALE * reference,
                                   reference, max(max_waits), 'p95')
        if problem.express:
            model.add_division_equality(floor_express, 2 * SCALE * timely, problem.express)
        else:
            model.add(floor_express == 2 * SCALE)
        if problem.potential:
            model.add_division_equality(floor_profit, 2 * SCALE * contribution,
                                        problem.potential)
        else:
            model.add(floor_profit == 0)
        scaled = model.new_int_var(0, 6 * SCALE, 'scaled_quality')
        model.add(scaled == floor_mean + floor_p95 + floor_express + floor_profit)
        objective = scaled
    if objective_component in COMPONENTS and COMPONENTS[objective_component][0] == 'min':
        model.minimize(objective)
    else:
        model.maximize(objective)

    def hint(dispatches):
        model.clear_hints()
        indexed = {d['job_id']: d for d in dispatches}
        for i, job in enumerate(jobs):
            d = indexed.get(job['job_id'])
            model.add_hint(starts[i], d['started_ms'] // tick if d else horizon)
            model.add_hint(active[i], int(d is not None))
        for i, m, x in assignments:
            if x is not None:
                d = indexed.get(jobs[i]['job_id'])
                model.add_hint(x, int(d is not None and d['printer'] == m))

    hint(best_dispatches)
    validation = model.validate()
    _require(not validation, f'invalid CP-SAT model: {validation}')
    proof['solves'] = []

    def run(phase):
        remaining = time_limit_s - (time.monotonic() - started)
        if remaining <= 0:
            return None, None
        solver = cp_model.CpSolver()
        solver.parameters.max_time_in_seconds = (remaining * 0.8 if phase in (
            'scaled_maximize', 'linear_diagnostic') else remaining)
        solver.parameters.num_search_workers = workers
        solver.parameters.random_seed = seed
        solver.parameters.absolute_gap_limit = 0.0
        solver.parameters.relative_gap_limit = 0.0
        callback = None
        if phase in ('scaled_maximize', 'linear_diagnostic') or objective_component:
            class IncumbentCallback(cp_model.CpSolverSolutionCallback):
                def __init__(self):
                    super().__init__()
                    self.last_write = 0.0

                def on_solution_callback(self):
                    nonlocal global_upper
                    previous = _fraction_score(best_quality)
                    audit(self)
                    if objective_component in COMPONENTS:
                        update_component_bound(self, 'FEASIBLE')
                    if scaled is not None:
                        upper = ceil(self.best_objective_bound)
                        global_upper = min(global_upper, Fraction(100 * (upper + 4), 6 * SCALE))
                        proof['scaled_incumbent'] = self.value(scaled)
                        proof['scaled_upper_bound'] = upper
                    should_write = (_fraction_score(best_quality) > previous or
                                    time.monotonic() - self.last_write >= 2)
                    if objective_component == 'tangent':
                        should_write = should_write and _fraction_score(best_quality) > initial_score
                    if should_write:
                        _write_progress(progress_path, report())
                        self.last_write = time.monotonic()

            callback = IncumbentCallback()
        status = solver.solve(model, callback)
        proof['solves'].append({'phase': phase, 'status': solver.status_name(status),
                                'seconds': solver.wall_time,
                                'branches': solver.num_branches,
                                'conflicts': solver.num_conflicts,
                                'objective': solver.objective_value,
                                'objective_bound': solver.best_objective_bound})
        return solver, status

    def audit(solver):
        nonlocal best_dispatches, best_quality
        dispatches = []
        for i, m, x in assignments:
            if x is not None and solver.boolean_value(x):
                start_ms = solver.value(starts[i]) * tick
                dispatches.append({'job_id': jobs[i]['job_id'], 'printer': m,
                                   'started_ms': start_ms,
                                   'finished_ms': start_ms + problem.duration[i][m]})
        dispatches.sort(key=lambda d: (d['started_ms'], d['printer'], d['job_id']))
        quality = evaluate(scenario, dispatches)
        aggregate = tuple(solver.value(v) for v in aggregates)
        recomputed = (Fraction(quality['average_observed_wait_ms_fraction']) * n // tick,
                      quality['p95_observed_wait_ms'] // tick,
                      quality['express_on_time'], min(problem.potential, quality['contribution_cents']))
        _require(aggregate == recomputed, f'CP-SAT aggregate audit mismatch: {aggregate} != {recomputed}')
        value = _scaled_objective(problem, aggregate, tick)
        if scaled is not None:
            _require(solver.value(scaled) == value, 'scaled objective audit mismatch')
        if _fraction_score(quality) > _fraction_score(best_quality):
            best_dispatches, best_quality = dispatches, quality
        return aggregate, quality, dispatches, value

    def update_component_bound(solver, solver_status):
        nonlocal component_bound
        primal = solver.value(component_variables[objective_component])
        sense, unit = COMPONENTS[objective_component]
        certified = (primal if solver_status == 'OPTIMAL' else
                     ceil(solver.best_objective_bound) if sense == 'max' else
                     floor(solver.best_objective_bound))
        certified = max(0, certified)
        # A baseline Q and a component-optimal schedule need not coincide.  Keep
        # this certificate's own schedule so its primal value can be audited.
        own_dispatches = []
        for i, m, x in assignments:
            if x is not None and solver.boolean_value(x):
                start_ms = solver.value(starts[i]) * tick
                own_dispatches.append({'job_id': jobs[i]['job_id'], 'printer': m,
                                       'started_ms': start_ms,
                                       'finished_ms': start_ms + problem.duration[i][m]})
        own_dispatches.sort(key=lambda d: (d['started_ms'], d['printer'], d['job_id']))
        component_bound = {'schema_version': 1, 'kind': 'campus-print-component-bound',
                           'scenario_sha256': fingerprint,
                           'objective_component': objective_component, 'sense': sense,
                           'unit': unit, 'tick_ms': tick, 'solver_status': solver_status,
                           'primal_value': primal, 'certified_bound': certified,
                           'solver_version': proof['solver_version'],
                           'used_component_bounds': [
                               {'objective_component': b['objective_component'],
                                'certified_bound': b['certified_bound'],
                                'solver_status': b['solver_status']}
                               for b in applied_bounds],
                           'dispatches': own_dispatches,
                           'quality': evaluate(scenario, own_dispatches)}

    phase = (objective_component + '_component' if objective_component else
             'linear_diagnostic' if linear_components else 'scaled_maximize')
    solver, status = run(phase)
    if status is not None:
        proof['phase_one_status'] = solver.status_name(status)
    if status in (cp_model.FEASIBLE, cp_model.OPTIMAL):
        aggregate, quality, dispatches, value = audit(solver)
        if objective_component == 'tangent':
            termination = 'tangent_proxy_optimal' if status == cp_model.OPTIMAL else 'tangent_time_limit'
        elif objective_component:
            update_component_bound(solver, solver.status_name(status))
            termination = 'component_optimal' if status == cp_model.OPTIMAL else 'component_time_limit'
        elif not linear_components:
            bound = ceil(solver.best_objective_bound)
            global_upper = min(global_upper, Fraction(100 * (bound + 4), 6 * SCALE))
        if not linear_components and not objective_component:
            if status == cp_model.OPTIMAL:
                proof['scaled_objective_optimal'] = value
            model.clear_objective()
            threshold = (6 * SCALE * _fraction_score(best_quality) / 100).__floor__() - 3
            proof['aggregate_search_initial_threshold'] = threshold
            model.add(scaled >= threshold)
            # Excluding this aggregate is sound because its exact score has
            # already been audited; other schedules with it have the same score.
            pending = (aggregate, quality, dispatches, value)
            while True:
                if pending is not None:
                    aggregate, quality, dispatches, value = pending
                    proof['aggregate_tuples'].append({
                        'sum_wait_ticks': aggregate[0], 'p95_ticks': aggregate[1],
                        'express_on_time': aggregate[2], 'capped_contribution_cents': aggregate[3],
                        'scaled_objective': value,
                        'quality_score_fraction': quality['quality_score_fraction'],
                        'dispatches_sha256': hashlib.sha256(json.dumps(
                            dispatches, sort_keys=True).encode('utf-8')).hexdigest()})
                    model.add_forbidden_assignments(aggregates, [aggregate])
                    # The original score decreases in A/B and increases in T/G.
                    # Hence no tuple weakly dominated by an audited tuple can
                    # improve on the incumbent, including a possible equal tie.
                    improving = [model.new_bool_var(f'improve_{len(proof["aggregate_tuples"])}_{k}')
                                 for k in range(4)]
                    model.add(total_wait < aggregate[0]).only_enforce_if(improving[0])
                    model.add(p95 < aggregate[1]).only_enforce_if(improving[1])
                    model.add(timely > aggregate[2]).only_enforce_if(improving[2])
                    model.add(contribution > aggregate[3]).only_enforce_if(improving[3])
                    model.add_bool_or(improving)
                    improved_threshold = (6 * SCALE * _fraction_score(best_quality) / 100).__floor__() - 3
                    if improved_threshold > threshold:
                        threshold = improved_threshold
                        model.add(scaled >= threshold)
                    hint(best_dispatches)
                    _write_progress(progress_path, report())
                solver, status = run('exact_aggregate_exhaustion')
                if status is None:
                    break
                proof['phase_two_status'] = solver.status_name(status)
                if status == cp_model.INFEASIBLE:
                    complete, termination = True, 'all_potentially_better_aggregate_tuples_exhausted'
                    break
                if status not in (cp_model.FEASIBLE, cp_model.OPTIMAL):
                    break
                pending = audit(solver)
                # Satisfaction solves normally return OPTIMAL, but FEASIBLE is
                # equally sufficient for auditing and excluding this tuple.
        elif linear_components:
            termination = 'linear_diagnostic_complete' if status == cp_model.OPTIMAL else 'time_limit'
    elif status == cp_model.MODEL_INVALID:
        raise ValueError(f'CP-SAT rejected model: {solver.response_stats()}')
    result = report()
    if objective_component != 'tangent' or _fraction_score(best_quality) > initial_score:
        _write_progress(progress_path, result)
    return result


def solve_document_cp(document, **kwargs):
    _require(document.get('kind') == 'campus-print-optimal-input' and
             document.get('schema_version') == 1, 'unsupported input format')
    objective = document['objective']
    _require(objective['name'] == OBJECTIVE and objective['wait_reference_ms'] == REFERENCE_MS and
             all(objective[k] == 1 for k in ('waiting_weight', 'express_weight', 'contribution_weight')),
             'objective must remain the fixed equal-weight production objective')
    return {'schema_version': 1, 'kind': 'campus-print-optimal-result', 'objective': objective,
            'scope': 'fixed known fresh orders; no failures, restocking or unknown future arrivals; intentional idling allowed',
            'scenarios': [solve_scenario_cp(scenario, **kwargs) for scenario in document['scenarios']]}


def main(argv=None):
    global SCALE
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--progress', type=Path)
    parser.add_argument('--time-limit', type=float, default=300)
    parser.add_argument('--workers', type=int, default=8)
    parser.add_argument('--seed', type=int, default=20261020)
    parser.add_argument('--scale', type=int, default=SCALE,
                        help='Integer envelope scale; the final certificate still uses the original exact Q')
    parser.add_argument('--linear-components', action='store_true')
    parser.add_argument('--no-linear-chords', action='store_true')
    parser.add_argument('--no-resource-cuts', action='store_true')
    parser.add_argument('--objective-component', choices=tuple(COMPONENTS) + ('tangent',))
    parser.add_argument('--certified-bounds', type=Path, action='append', default=[])
    args = parser.parse_args(argv)
    try:
        _require(1 <= args.scale <= 1000000000, 'scale must be in 1..1000000000')
        SCALE = args.scale
        _require(args.input.resolve() != args.output.resolve(), 'input and output must differ')
        _require(not args.output.exists(), 'output must be a new file')
        raw = args.input.read_bytes()
        bounds = [json.loads(path.read_text(encoding='utf-8-sig')) for path in args.certified_bounds]
        report = solve_document_cp(json.loads(raw.decode('utf-8-sig')),
                                   time_limit_s=args.time_limit, workers=args.workers,
                                   seed=args.seed, progress_path=args.progress,
                                   linear_components=args.linear_components,
                                   linear_chords=not args.no_linear_chords,
                                   resource_cuts=not args.no_resource_cuts,
                                   objective_component=args.objective_component,
                                   certified_bounds=bounds)
        report['evidence'] = {'input_sha256': hashlib.sha256(raw).hexdigest(),
                              'solver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                              'python_version': sys.version.split()[0]}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        for result in report['scenarios']:
            print(f'{result["name"]}: {result["status"]}, score={result["quality"]["overall_score"]:.10f}, gap={result["proof"]["absolute_gap_fraction"]}')
        if args.objective_component:
            return 0 if all(r.get('component_bound', {}).get('solver_status') == 'OPTIMAL'
                            for r in report['scenarios']) else 2
        return 0 if all(r['status'] == 'OPTIMAL' for r in report['scenarios']) else 2
    except (KeyError, TypeError, ValueError, OSError, ImportError) as error:
        print(f'CP schedule: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
