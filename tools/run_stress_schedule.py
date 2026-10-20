"""Run the fixed 100-order stress case, audit C++ baselines, and certify Q."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from solve_optimal_schedule import OBJECTIVE, REFERENCE_MS, evaluate, _require


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def assemble(scenario, optimal, baselines, evidence):
    source = dict(scenario)
    source.pop('baselines', None)
    serialized = canonical(source)
    runs = []
    for baseline in baselines:
        audited = evaluate(source, baseline['dispatches'])
        for key, value in baseline['quality'].items():
            if key in audited:
                _require(abs(audited[key] - value) <= 1e-8 * max(1, abs(value)),
                         f'C++ quality differs: {baseline["policy"]}.{key}')
        runs.append({'policy': baseline['policy'], 'status': 'BASELINE',
                     'dispatches': baseline['dispatches'], 'quality': audited})
    quality = evaluate(source, optimal['dispatches'])
    _require(quality['quality_score_fraction'] == optimal['quality']['quality_score_fraction'],
             'optimal schedule audit mismatch')
    _require(Fraction(optimal['proof']['lower_bound_fraction']) == Fraction(quality['quality_score_fraction']),
             'proof lower bound differs from audited schedule')
    if optimal['status'] == 'OPTIMAL':
        _require(optimal['proof']['complete'] and
                 optimal['proof']['lower_bound_fraction'] == optimal['proof']['upper_bound_fraction'] and
                 Fraction(optimal['proof']['absolute_gap_fraction']) == 0,
                 'an incomplete proof cannot claim OPTIMAL')
    runs.append({'policy': 'optimal', 'status': optimal['status'],
                 'dispatches': optimal['dispatches'], 'quality': quality, 'proof': optimal['proof']})
    return {'schema_version': 1, 'kind': 'campus-print-stress-comparison',
            'scenario': source, 'scenario_json': serialized,
            'scenario_sha256': hashlib.sha256(serialized.encode('utf-8')).hexdigest(),
            'runs': runs, 'evidence': evidence,
            'scope': 'fixed known 100 orders; original equal-weight quality; active jobs non-preemptive; intentional idling allowed'}


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--input', required=True, type=Path)
    cli.add_argument('--replay', required=True, type=Path)
    cli.add_argument('--output-dir', required=True, type=Path)
    cli.add_argument('--time-limit', type=float, default=600)
    cli.add_argument('--workers', type=int, default=8)
    cli.add_argument('--seed', type=int, default=20261020)
    cli.add_argument('--scale', type=int, default=1000000000)
    cli.add_argument('--resume', type=Path, help='Previously audited incumbent or comparison JSON')
    cli.add_argument('--certified-bounds', action='append', type=Path, default=[],
                     help='Matching independently solved component bounds; repeat for each component')
    args = cli.parse_args(argv)
    try:
        import solve_cp_schedule as cp
        _require(1 <= args.scale <= 1000000000, 'scale must be in 1..1000000000')
        cp.SCALE = args.scale
        solve_scenario_cp = cp.solve_scenario_cp
        _require(not args.output_dir.exists(), 'output directory must be new')
        raw = args.input.read_bytes()
        document = json.loads(raw.decode('utf-8-sig'))
        _require(document['kind'] == 'campus-print-optimal-input' and len(document['scenarios']) == 1,
                 'expected exactly one fixed stress scenario')
        objective = document['objective']
        _require(objective['name'] == OBJECTIVE and objective['wait_reference_ms'] == REFERENCE_MS and
                 all(objective[k] == 1 for k in ('waiting_weight', 'express_weight', 'contribution_weight')),
                 'the original equal-weight quality objective is required')
        scenario = document['scenarios'][0]
        _require(len(scenario['jobs']) == 100, 'stress comparison requires exactly 100 orders')
        args.output_dir.mkdir(parents=True)
        cpp_path = args.output_dir / 'cpp-baselines.json'
        command = [str(args.replay.resolve()), '--input', str(args.input.resolve()), '--output', str(cpp_path.resolve())]
        subprocess.run(command, check=True, capture_output=True, timeout=30)
        cpp = json.loads(cpp_path.read_text(encoding='utf-8'))
        _require({r['policy'] for r in cpp['runs']} == {'fcfs', 'sjf'}, 'only FCFS and SJF baselines are allowed')
        scenario['baselines'] = cpp['runs']
        if args.resume:
            previous = json.loads(args.resume.read_text(encoding='utf-8'))
            if previous.get('kind') == 'campus-print-stress-comparison':
                _require(canonical(previous['scenario']) == canonical({k: v for k, v in scenario.items() if k != 'baselines'}),
                         'resume input does not match this scenario')
                prior = next(r for r in previous['runs'] if r['policy'] == 'optimal')
            else:
                prior = previous
            evaluate(scenario, prior['dispatches'])
            scenario['baselines'] = list(cpp['runs']) + [{'policy': 'prior_incumbent', 'dispatches': prior['dispatches']}]
        evidence = {'input_sha256': hashlib.sha256(raw).hexdigest(),
                    'cp_solver_sha256': hashlib.sha256((Path(__file__).parent / 'solve_cp_schedule.py').read_bytes()).hexdigest(),
                    'audit_solver_sha256': hashlib.sha256((Path(__file__).parent / 'solve_optimal_schedule.py').read_bytes()).hexdigest(),
                    'replay_sha256': hashlib.sha256(args.replay.read_bytes()).hexdigest(),
                    'replay_command': command, 'python_version': sys.version.split()[0],
                    'time_limit_s': args.time_limit, 'workers': args.workers, 'seed': args.seed,
                    'integer_envelope_scale': args.scale,
                    'optimizer_arguments': list(argv) if argv is not None else sys.argv[1:]}
        certified = [json.loads(path.read_text(encoding='utf-8')) for path in args.certified_bounds]
        evidence['component_bound_files'] = [
            {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in args.certified_bounds]
        for source_name in ('solve_cp_schedule.py', 'solve_optimal_schedule.py', 'generate_stress_schedule.py',
                            'run_stress_schedule.py'):
            (args.output_dir / source_name).write_bytes((Path(__file__).parent / source_name).read_bytes())
        best = max(cpp['runs'], key=lambda r: Fraction(evaluate(scenario, r['dispatches'])['quality_score_fraction']))
        initial = {'status': 'FEASIBLE', 'dispatches': best['dispatches'], 'quality': evaluate(scenario, best['dispatches']),
                   'proof': {'complete': False, 'lower_bound_fraction': evaluate(scenario, best['dispatches'])['quality_score_fraction'],
                             'upper_bound_fraction': '100', 'absolute_gap_fraction': str(100 - Fraction(evaluate(scenario, best['dispatches'])['quality_score_fraction']))}}
        write_json(args.output_dir / 'report.json', assemble(scenario, initial, cpp['runs'], evidence))
        begin = time.monotonic()
        optimal = solve_scenario_cp(scenario, time_limit_s=args.time_limit, workers=args.workers,
                                    seed=args.seed, progress_path=args.output_dir / 'progress.json',
                                    certified_bounds=certified)
        evidence['elapsed_seconds'] = time.monotonic() - begin
        report = assemble(scenario, optimal, cpp['runs'], evidence)
        write_json(args.output_dir / 'report.json', report)
        for run in report['runs']:
            print(f'{run["policy"]}: {run["status"]}, score={run["quality"]["overall_score"]:.9f}, completed={run["quality"]["completed"]}, express={run["quality"]["express_on_time"]}/{run["quality"]["express_accepted"]}')
        print(f'Exact gap: {optimal["proof"]["absolute_gap_fraction"]}')
        return 0 if optimal['status'] == 'OPTIMAL' else 2
    except (ValueError, KeyError, TypeError, OSError, ImportError, subprocess.CalledProcessError) as error:
        print(f'stress schedule: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
