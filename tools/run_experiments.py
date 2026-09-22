#!/usr/bin/env python3
"""Run production conversion/replay with fixed parameters and independently audit each trace."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def content_digest(document):
    # Conversion/download dates differ on rerun; all other source metadata and jobs are significant.
    stable = json.loads(json.dumps(document))
    for key in ('converted_at_utc', 'retrieved_at_utc'):
        stable.get('source', {}).pop(key, None)
    return hashlib.sha256(json.dumps(stable, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def audit(workload, result, options):
    """Validate raw dispatches and recompute metrics; never trust summary fields alone."""
    inputs = {j['job_id']: j for j in workload['jobs']}
    dispatched = result['dispatches']
    require(len(dispatched) == len(inputs), 'dispatch count mismatch')
    seen, waits, turnarounds = set(), [], []
    available = [result['started_ms']] * options['printers']
    busy = 0
    output_rows = {j['job_id']: j for j in result['jobs']}
    require(len(output_rows) == len(inputs), 'result job count mismatch')
    require(set(output_rows) == set(inputs), 'result identities mismatch')
    for event in dispatched:
        ident = event['job_id']
        require(ident in inputs and ident not in seen, 'unknown/duplicate dispatched job')
        seen.add(ident)
        job = inputs[ident]
        printer = event['printer']
        require(isinstance(printer, int) and 0 <= printer < len(available), 'invalid printer')
        start, end = event['started_ms'], event['finished_ms']
        require(start >= job['submitted_ms'] and start >= available[printer], 'premature arrival or slot overlap')
        duration = job['pages'] * job['copies'] * options['page_ms']
        require(end - start == duration, 'service-time mismatch')
        available[printer] = end
        busy += duration
        waits.append(start - job['submitted_ms'])
        turnarounds.append(end - job['submitted_ms'])
        row = output_rows[ident]
        require(row['state'] == 'completed' and row['first_started_ms'] == start and row['finished_ms'] == end,
                'job timing disagrees with dispatch trace')
        require(row['waiting_ms'] == waits[-1] and row['turnaround_ms'] == turnarounds[-1], 'job metric mismatch')
    require(result['started_ms'] == min(j['submitted_ms'] for j in inputs.values()), 'window start mismatch')
    require(result['finished_ms'] == max(available), 'window end mismatch')
    total_ms = result['finished_ms'] - result['started_ms']
    n = len(inputs)
    expected = {'average_first_wait_ms': sum(waits) / n, 'max_first_wait_ms': max(waits),
                'average_total_queue_wait_ms': sum(waits) / n, 'average_turnaround_ms': sum(turnarounds) / n,
                'busy_printer_ms': busy, 'throughput_per_second': n * 1000 / total_ms,
                'utilization': busy / (options['printers'] * total_ms)}
    require(result['metrics']['completed'] == n and result['metrics']['failed'] == 0, 'terminal counts mismatch')
    for key, value in expected.items():
        require(math.isclose(result['metrics'][key], value, rel_tol=1e-10, abs_tol=1e-7), f'metric mismatch: {key}')
    return {'p95_wait_ms': sorted(waits)[math.ceil(0.95 * n) - 1], 'audited_jobs': n}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', required=True, type=Path)
    parser.add_argument('--swf', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='new directory; existing evidence is never overwritten')
    parser.add_argument('--config', type=Path, default=ROOT / 'experiments/config.json')
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding='utf-8'))
    require(digest(args.swf) == config['source_sha256'], 'SWF checksum differs from the fixed experiment source')
    require(not args.output.exists(), 'output directory already exists; choose a new directory')
    args.output.mkdir(parents=True)
    replay = args.replay.resolve()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    tracked_dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip()
    manifest = {'schema_version': 1, 'started_at_utc': datetime.now(timezone.utc).isoformat(),
                'git_revision': revision, 'tracked_worktree_dirty': bool(tracked_dirty),
                'platform': platform.platform(), 'python': platform.python_version(),
                'config': config, 'config_sha256': digest(args.config), 'replay_binary_sha256': digest(replay),
                'source_sha256': digest(args.swf), 'cases': [], 'commands': [], 'files': {}}
    manifest['implementation_sha256'] = {path: digest(ROOT / path) for path in [
        'src/core/scheduler.cpp', 'src/core/replay.cpp', 'src/metrics/statistics.cpp',
        'tools/replay_main.cpp', 'tools/convert_swf.py', 'tools/run_experiments.py']}
    summary = []

    def command(parts):
        manifest['commands'].append([str(p) for p in parts])
        run = subprocess.run([str(p) for p in parts], capture_output=True, text=True, encoding='utf-8',
                             errors='replace', timeout=120)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        return run.stdout.strip()

    def run_case(name, workload_path, case):
        report_path = args.output / 'reports' / f'{name}.json'
        repeat_path = args.output / 'reports' / f'{name}.repeat.json'
        report_path.parent.mkdir(parents=True, exist_ok=True)
        options = {'printers': config['printers'], 'page_ms': config['page_ms'], 'aging_ms': case['aging_ms']}
        base = [replay, '--input', workload_path, '--printers', options['printers'],
                '--page-ms', options['page_ms'], '--aging-ms', options['aging_ms']]
        command([*base, '--output', report_path])
        command([*base, '--output', repeat_path])
        require(report_path.read_bytes() == repeat_path.read_bytes(), f'non-deterministic report: {name}')
        document = json.loads(workload_path.read_text(encoding='utf-8'))
        report = json.loads(report_path.read_text(encoding='utf-8'))
        require(report['workload_sha256'] == digest(workload_path), 'input hash mismatch')
        rows = []
        for result in report['results']:
            checked = audit(document, result, options)
            low = next((j for j in result['jobs'] if j['job_id'] == 'low-priority-large'), None)
            row = {'case': name, **case, 'policy': result['policy'], 'count': len(document['jobs']),
                   'workload_content_sha256': content_digest(document), **result['metrics'], **checked,
                   'makespan_ms': result['finished_ms'] - result['started_ms'],
                   'low_priority_wait_ms': low['waiting_ms'] if low else None,
                   'clipped_jobs': document.get('source', {}).get('mapping', {}).get('clipped_jobs', 0)}
            rows.append(row)
        if case.get('priority_mode') == 'uniform':
            require(report['results'][0]['dispatches'] == report['results'][2]['dispatches'],
                    'uniform priorities must match FCFS in this no-retry model')
        summary.extend(rows)
        manifest['cases'].append({'name': name, **case, 'input': workload_path.relative_to(args.output).as_posix(),
                                  'report': report_path.relative_to(args.output).as_posix(), 'repeat_identical': True,
                                  'workload_content_sha256': content_digest(document)})
        for file in (workload_path, report_path, repeat_path):
            manifest['files'][file.relative_to(args.output).as_posix()] = digest(file)
        print(f'Completed {name}: {len(document["jobs"])} jobs x 3 policies, repeat/audit passed', flush=True)

    for offset in config['offsets']:
        for mode in config['priority_modes']:
            name = f'sdsc-offset{offset}-{mode}'
            case = {'family': 'windows', 'offset': offset, 'priority_mode': mode,
                    'time_scale': config['time_scale'], 'aging_ms': config['aging_ms']}
            path = args.output / 'inputs' / f'{name}.json'
            command([sys.executable, ROOT / 'tools/convert_swf.py', '--input', args.swf.resolve(),
                     '--source-url', config['source_url'], '--output', path, '--count', config['count'],
                     '--offset', offset, '--priority-mode', mode, '--priority-levels', config['priority_levels'],
                     '--time-scale', config['time_scale'], '--seconds-per-page', config['seconds_per_page'],
                     '--max-pages', config['max_pages']])
            run_case(name, path, case)
    for scale in config['sensitivity_time_scales']:
        name = f'sdsc-density-{scale}'
        path = args.output / 'inputs' / f'{name}.json'
        command([sys.executable, ROOT / 'tools/convert_swf.py', '--input', args.swf.resolve(),
                 '--source-url', config['source_url'], '--output', path, '--count', config['count'], '--offset', '0',
                 '--priority-mode', 'job-id-modulo', '--priority-levels', config['priority_levels'],
                 '--time-scale', scale, '--seconds-per-page', config['seconds_per_page'], '--max-pages', config['max_pages']])
        run_case(name, path, {'family': 'density', 'offset': 0, 'priority_mode': 'job-id-modulo',
                             'time_scale': scale, 'aging_ms': config['aging_ms']})
    # Finite stream: 2 high-priority tasks occupy the printers every 1500 ms.
    # This diagnoses delayed large tasks; it is not a proof about infinite-arrival starvation.
    pressure = [{'job_id': 'low-priority-large', 'title': 'Low-priority large job', 'pages': 40,
                 'copies': 1, 'submitted_ms': 0, 'priority': 0}]
    for wave in range(20):
        for slot in range(2):
            pressure.append({'job_id': f'urgent-{wave}-{slot}', 'title': 'Synthetic urgent job', 'pages': 10,
                             'copies': 1, 'submitted_ms': wave * 1500, 'priority': 5})
    path = args.output / 'inputs/aging-pressure.json'
    write_json(path, {'schema_version': 1, 'kind': 'campus-print-workload',
                     'source': {'type': 'handwritten-synthetic', 'randomness': 'none'}, 'jobs': pressure})
    for aging in config['pressure_aging_ms']:
        run_case(f'aging-pressure-{aging}', path, {'family': 'aging-pressure', 'offset': None,
                 'priority_mode': 'synthetic-urgency', 'time_scale': None, 'aging_ms': aging})
    write_json(args.output / 'summary.json', summary)
    manifest['files']['summary.json'] = digest(args.output / 'summary.json')
    manifest['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
    manifest['audited_policy_runs'] = len(summary)
    write_json(args.output / 'manifest.json', manifest)
    print(f'Wrote {len(summary)} policy results and all inputs/raw reports to {args.output}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'run_experiments: {error}', file=sys.stderr)
        raise SystemExit(1)
