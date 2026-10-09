"""Offline end-to-end checks for SWF conversion and the real replay executable."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PARSER = argparse.ArgumentParser()
PARSER.add_argument('--replay', required=True, type=Path)
PARSER.add_argument('--converter', required=True, type=Path)
ARGS, UNIT_ARGS = PARSER.parse_known_args()
REPLAY = ARGS.replay.resolve()
CONVERTER = ARGS.converter.resolve()


def swf(job_id, submit, runtime, status=1):
    return ' '.join(map(str, [job_id, submit, -1, runtime, 8, -1, -1, -1, -1, -1, status, 42, -1, -1, -1, -1, -1, -1]))


def job(job_id='a', pages=1, arrival=0):
    return dict(job_id=job_id, title='中文任务', pages=pages, copies=1, submitted_ms=arrival, priority=0)


class WorkloadTools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='校园 打印 A-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.swf'
        self.workload = self.root / '任务.json'
        self.report = self.root / '结果.json'

    def invoke(self, command, success=True):
        result = subprocess.run([str(part) for part in command], capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=20)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, 'invalid input unexpectedly accepted')
        return result

    def convert(self, *options, success=True):
        return self.invoke([sys.executable, CONVERTER, '--input', self.source, '--output', self.workload,
                            '--count', '2', *options], success)

    def save(self, rows):
        self.workload.write_text(json.dumps(dict(schema_version=1, kind='campus-print-workload', jobs=rows)), encoding='utf-8')

    def replay(self, *options, success=True):
        return self.invoke([REPLAY, '--input', self.workload, '--output', self.report, *options], success)

    def test_swf_mapping_provenance_and_skips(self):
        raw = '\n'.join(['; Synthetic fixture, not SDSC data', swf(7, '10.0015', 61), swf(3, 10, 1),
                         swf(9, 11, 60000), swf(3, 12, 10), swf(12, 12, -1), swf(13, 12, 10, 0),
                         swf(14, 12, 'NaN'), 'not an 18-column row']) + '\n'
        self.source.write_text(raw, encoding='utf-8')
        self.convert('--count', '3', '--time-scale', '1', '--max-pages', '3',
                     '--priority-mode', 'job-id-modulo', '--priority-levels', '5')
        data = json.loads(self.workload.read_text(encoding='utf-8'))
        self.assertEqual([j['job_id'] for j in data['jobs']], ['swf-3', 'swf-7', 'swf-9'])
        self.assertEqual([j['submitted_ms'] for j in data['jobs']], [0, 2, 1000])
        self.assertEqual([j['pages'] for j in data['jobs']], [1, 2, 3])
        self.assertEqual([j['priority'] for j in data['jobs']], [3, 2, 4])
        self.assertEqual(data['source']['sha256'], hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertEqual(data['source']['headers'], ['; Synthetic fixture, not SDSC data'])
        self.assertEqual(sum(data['source']['skipped_records'].values()), 5)
        self.assertEqual(data['source']['mapping']['clipped_jobs'], 1)
        self.assertIsNone(data['source']['retrieved_at_utc'])
        self.replay()
        self.assertEqual(len(json.loads(self.report.read_text())['results']), 3)

    def test_offset_uniform_priority_and_insufficient_data(self):
        self.source.write_text('\n'.join([swf(1, 10, 10), swf(2, 20, 10), swf(3, 30, 10)]), encoding='utf-8')
        self.convert('--offset', '1')
        data = json.loads(self.workload.read_text())
        self.assertEqual([j['source_job_id'] for j in data['jobs']], ['2', '3'])
        self.assertEqual(data['jobs'][0]['submitted_ms'], 0)
        self.assertTrue(all(j['priority'] == 0 for j in data['jobs']))
        original = self.workload.read_bytes()
        self.convert('--count', '4', success=False)
        self.assertEqual(self.workload.read_bytes(), original)

    def test_converter_invalid_options_and_html(self):
        self.source.write_text('<html>not a trace</html>', encoding='utf-8')
        self.convert(success=False)
        self.assertFalse(self.workload.exists())
        for options in [('--time-scale', 'NaN'), ('--seconds-per-page', '0'), ('--count', '0'), ('--offset', '-1')]:
            with self.subTest(options=options):
                self.convert(*options, success=False)
        self.source.write_text(swf(1, 0, 1), encoding='utf-8')
        original = self.source.read_bytes()
        self.convert('--count', '1', '--output', self.source, success=False)
        self.assertEqual(self.source.read_bytes(), original)

    def test_replay_hand_values_and_repeatability(self):
        self.save([job('long', 20), job('short', 2), job('medium', 5)])
        self.replay('--printers', '1', '--page-ms', '1000')
        original = self.report.read_bytes()
        data = json.loads(original)
        results = {r['policy']: r for r in data['results']}
        self.assertEqual(results['fcfs']['metrics']['average_first_wait_ms'], 14000)
        self.assertEqual(results['sjf']['metrics']['average_first_wait_ms'], 3000)
        self.assertEqual(set(results), {'fcfs', 'sjf', 'balanced'})
        self.assertEqual([d['job_id'] for d in results['balanced']['dispatches']], ['short', 'medium', 'long'])
        self.assertEqual(data['workload_sha256'], hashlib.sha256(self.workload.read_bytes()).hexdigest())
        self.replay('--printers', '1', '--page-ms', '1000')
        self.assertEqual(self.report.read_bytes(), original)
        self.replay('--policy', 'sjf')
        self.assertEqual(len(json.loads(self.report.read_text())['results']), 1)
        for old in ('priority', 'deadline', 'revenue'):
            self.replay('--policy', old)
            self.assertEqual(json.loads(self.report.read_text())['results'][0]['policy'], 'balanced')

    def test_replay_future_arrivals_and_empty_workload(self):
        self.save([job('future', 1, 1000), job('present', 20)])
        self.replay('--policy', 'sjf', '--printers', '1', '--page-ms', '100')
        dispatches = json.loads(self.report.read_text())['results'][0]['dispatches']
        self.assertEqual([(d['job_id'], d['started_ms']) for d in dispatches], [('present', 0), ('future', 2000)])
        self.save([])
        self.replay()
        for result in json.loads(self.report.read_text())['results']:
            self.assertEqual(result['jobs'], [])
            self.assertEqual(result['metrics']['utilization'], 0)

    def test_replay_rejects_invalid_jobs_without_replacing_report(self):
        variants = [[job(), job()], [dict(job(), pages=0)], [dict(job(), pages=1.5)],
                    [dict(job(), submitted_ms=True)], [dict(job(), submitted_ms=-1)],
                    [dict(job(), priority=101)], [dict(job(), job_id='../bad')],
                    [dict(job(), pages=1000, copies=100)], [dict(job(), title=' ')]]
        self.report.write_bytes(b'existing report')
        for rows in variants:
            with self.subTest(rows=rows):
                self.save(rows); self.replay(success=False)
                self.assertEqual(self.report.read_bytes(), b'existing report')
        self.save([job() for _ in range(10001)])
        self.replay(success=False)

    def test_replay_schema_arguments_and_input_protection(self):
        for text in ['not JSON', '[]', '{"schema_version": 2}', '{"schema_version": 1, "kind": "wrong", "jobs": []}']:
            self.workload.write_text(text, encoding='utf-8'); self.replay(success=False)
        self.save([job()])
        for args in [('--printers', '0'), ('--page-ms', '0'), ('--aging-ms', '0'), ('--policy', 'typo')]:
            with self.subTest(args=args): self.replay(*args, success=False)
        original = self.workload.read_bytes()
        self.replay('--output', self.workload, success=False)
        self.assertEqual(self.workload.read_bytes(), original)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *UNIT_ARGS], verbosity=2)
