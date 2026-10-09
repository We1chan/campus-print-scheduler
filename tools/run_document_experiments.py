"""Build reproducible local document workloads and verify production shop replays.

PDF page trees and PPTX slide lists are counted. Word cached page counts and
spreadsheet pagination estimates are identified separately. No document names,
contents or local paths are put into workload/report job titles.
"""
from __future__ import annotations
import argparse
from collections import Counter
import csv
import hashlib
from html.parser import HTMLParser
import json
import logging
import math
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET
from zipfile import ZipFile
from run_scheduling_benchmark import audit_run


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def table_pages(rows, columns):
    # Explicit synthetic A4 layout: 45 rows and 8 columns per page.
    return max(1, math.ceil(rows/45) * math.ceil(columns/8))


def spreadsheet_xml(path):
    counts, rows, columns = [], 0, 0
    for _, element in ET.iterparse(path, events=('end',)):
        tag = element.tag.rsplit('}', 1)[-1]
        if tag == 'Row':
            rows += 1
            column = 0
            for cell in element:
                if cell.tag.rsplit('}', 1)[-1] == 'Cell':
                    index = next((v for k, v in cell.attrib.items() if k.rsplit('}', 1)[-1] == 'Index'), None)
                    column = int(index) if index else column + 1
            columns = max(columns, column)
            element.clear()
        elif tag == 'Worksheet':
            counts.append(table_pages(rows, columns)); rows = columns = 0
            element.clear()
    if not counts:
        raise ValueError('no SpreadsheetML worksheets')
    return sum(counts)


class HtmlTable(HTMLParser):
    def __init__(self):
        super().__init__(); self.rows = 0; self.columns = 0; self.current = 0

    def handle_starttag(self, tag, attrs):
        if tag == 'tr':
            self.rows += 1; self.current = 0
        elif tag in ('td', 'th'):
            self.current += 1; self.columns = max(self.columns, self.current)


def measure_document(path):
    extension = path.suffix.lower()
    if extension == '.pdf':
        from pypdf import PdfReader
        with path.open('rb') as stream:
            reader = PdfReader(stream, strict=False)
            if reader.is_encrypted and not reader.decrypt(''):
                raise ValueError('PDF requires a password')
            return len(reader.pages), 'pdf_page_tree', 'exact_pages'
    if extension in ('.pptx', '.docx', '.xlsx'):
        with ZipFile(path) as archive:
            if extension == '.pptx':
                root = ET.fromstring(archive.read('ppt/presentation.xml'))
                count = len(root.findall('{*}sldIdLst/{*}sldId'))
                if not count:
                    raise ValueError('presentation has no slides')
                return count, 'pptx_slide_list', 'exact_slides'
            if extension == '.docx':
                if 'docProps/app.xml' in archive.namelist():
                    root = ET.fromstring(archive.read('docProps/app.xml'))
                    pages = root.find('{*}Pages')
                    if pages is not None and pages.text and int(pages.text) > 0:
                        return int(pages.text), 'word_saved_page_count', 'cached_pages'
                root = ET.fromstring(archive.read('word/document.xml'))
                characters = sum(len(t.text or '') for t in root.findall('.//{*}t'))
                breaks = sum(b.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}type') == 'page'
                             for b in root.findall('.//{*}br'))
                return max(1, math.ceil(characters/1800), breaks+1), 'word_1800_chars_per_page', 'estimated_pages'
            total = 0
            for name in archive.namelist():
                if not name.startswith('xl/worksheets/sheet') or not name.endswith('.xml'):
                    continue
                root = ET.fromstring(archive.read(name))
                rows = root.findall('{*}sheetData/{*}row')
                columns = max((len(row.findall('{*}c')) for row in rows), default=0)
                total += table_pages(len(rows), columns)
            return max(1, total), 'xlsx_45_rows_8_columns', 'estimated_pages'
    if extension in ('.doc', '.ppt', '.xls'):
        with path.open('rb') as stream:
            magic = stream.read(512)
        if magic.startswith(bytes.fromhex('d0cf11e0a1b11ae1')):
            if extension == '.xls':
                import xlrd
                book = xlrd.open_workbook(str(path), on_demand=True)
                try:
                    return sum(table_pages(s.nrows, s.ncols) for s in book.sheets()), 'xls_45_rows_8_columns', 'estimated_pages'
                finally:
                    book.release_resources()
            import olefile
            with olefile.OleFileIO(path) as ole:
                stream_name = '\x05DocumentSummaryInformation' if extension == '.ppt' else '\x05SummaryInformation'
                props = ole.getproperties(stream_name) if ole.exists(stream_name) else {}
                pages = props.get(7 if extension == '.ppt' else 14, 0)
                if isinstance(pages, int) and pages > 0:
                    return pages, 'legacy_office_saved_count', 'cached_pages'
                characters = props.get(16, 0)
                if extension == '.doc' and isinstance(characters, int) and characters > 0:
                    return max(1, math.ceil(characters/1800)), 'legacy_word_1800_chars_per_page', 'estimated_pages'
            raise ValueError('legacy Office file has no saved page/slide count')
        if extension == '.xls' and b'<?xml' in magic:
            return spreadsheet_xml(path), 'spreadsheetml_45_rows_8_columns', 'estimated_pages'
        if extension == '.xls' and b'<html' in magic.lower():
            parser = HtmlTable()
            with path.open('r', encoding='utf-8-sig', errors='replace') as stream:
                for block in iter(lambda: stream.read(65536), ''):
                    parser.feed(block)
            return table_pages(parser.rows, parser.columns), 'excel_html_45_rows_8_columns', 'estimated_pages'
        raise ValueError('unrecognized legacy Office container')
    if extension in ('.csv', '.tsv'):
        with path.open('r', encoding='utf-8-sig', errors='replace', newline='') as stream:
            rows = list(csv.reader(stream, delimiter='\t' if extension == '.tsv' else ','))
        return table_pages(len(rows), max(map(len, rows), default=0)), 'text_table_45_rows_8_columns', 'estimated_pages'
    raise ValueError('unsupported document format')


def extract(dataset, output):
    manifest = dataset / 'manifest.json'
    records = json.loads(manifest.read_text(encoding='utf-8'))
    if any(r['extension'] == '.pdf' for r in records):
        import pypdf  # Missing the PDF reader must not silently estimate every PDF.
    documents = []
    for index, record in enumerate(sorted(records, key=lambda r: r['sha256'])):
        path = (dataset / record['relative_path']).resolve()
        path.relative_to(dataset.resolve())
        if path.stat().st_size != record['bytes'] or sha256(path) != record['sha256']:
            raise ValueError(f'document {index+1}: source hash/size mismatch')
        error = ''
        try:
            pages, method, confidence = measure_document(path)
            if pages < 1:
                raise ValueError('empty document')
        except Exception as exc:
            # Keep every source represented, and make a fallback an explicit
            # synthetic assumption rather than calling it measured pagination.
            pages = max(1, math.ceil(record['bytes']/16384))
            method, confidence, error = 'fallback_16KiB_per_page', 'fallback_pages', str(exc)
        documents.append({'job_id': 'doc-' + record['sha256'][:20], 'sha256': record['sha256'],
                          'relative_path': record['relative_path'], 'extension': record['extension'],
                          'bytes': record['bytes'], 'raw_page_units': pages, 'pages': min(1000, pages),
                          'clipped': pages > 1000, 'method': method, 'confidence': confidence, 'error': error})
        if (index+1) % 100 == 0:
            print(f'Inspected and hash-checked {index+1}/{len(records)} documents', flush=True)
    write_json(output / 'document-metrics.json', documents)
    with (output / 'document-metrics.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(documents[0]))
        writer.writeheader(); writer.writerows(documents)
    return documents


def workload(documents, scenario):
    arrival_span, copies = {'day': (4*3600000, 1), 'peak': (30*60000, 2), 'batch': (0, 1)}[scenario]
    jobs = []
    for document in documents:
        digest = bytes.fromhex(document['sha256'])
        draw = int.from_bytes(digest[:8], 'big')
        slides = document['extension'] in ('.ppt', '.pptx')
        j = {'job_id': document['job_id'], 'title': document['job_id'], 'pages': document['pages'],
             'copies': copies, 'submitted_ms': draw % arrival_span if arrival_span else 0,
             'priority': digest[8] % 21, 'paper': 'a3' if digest[9] % 20 == 0 else 'a4',
             'color': slides or digest[10] % 10 < 2, 'duplex': not slides and digest[11] % 4 != 0,
             'service': ('express', 'standard', 'flexible')[digest[12] % 3]}
        jobs.append(j)
    jobs.sort(key=lambda j: (j['submitted_ms'], j['job_id']))
    return {'schema_version': 1, 'kind': 'campus-print-workload',
            'source': {'kind': 'local-university-documents', 'files': len(documents),
                       'payload_bytes': sum(d['bytes'] for d in documents),
                       'scenario': scenario, 'arrival_span_ms': arrival_span,
                       'mapping': 'Measured/cached/estimated page units capped at 1000; one slide per page. SHA-256-derived simulated arrivals, priorities, print options and service tiers; one order per file. No observed orders.'},
            'jobs': jobs}


def verify(report, input_workload):
    expected = {j['job_id']: j for j in input_workload['jobs']}
    if {r['policy'] for r in report['runs']} != {'fcfs', 'sjf', 'balanced'} or len(report['runs']) != 3:
        raise ValueError('expected exactly FCFS, SJF and balanced policy comparisons')
    fixed_inputs = None
    for run in report['runs']:
        audit_run(report, run)
        jobs = {j['job_id']: j for j in run['jobs']}
        if set(jobs) != set(expected):
            raise ValueError('lost or added orders')
        cohort = [(j['job_id'], j['pages'], j['copies'], j['submitted_ms'], j['priority'], j['paper'],
                   j['color'], j['duplex'], j['service'], j['quoted_cents'], j['deadline_ms']) for j in jobs.values()]
        if fixed_inputs is not None and cohort != fixed_inputs:
            raise ValueError('policy comparisons changed the input cohort')
        fixed_inputs = cohort
        for j in jobs.values():
            if any(j[k] != expected[j['job_id']][k] for k in ('pages', 'copies', 'submitted_ms', 'priority', 'paper', 'color', 'duplex', 'service')):
                raise ValueError('replay input differs from document workload')
        completed = [j for j in jobs.values() if j['state'] == 'completed']
        waits = sorted(j['first_started_ms']-j['submitted_ms'] for j in jobs.values() if j['first_started_ms'] is not None)
        checks = {'completed': len(completed), 'revenue_cents': sum(j['quoted_cents']-j['refund_cents'] for j in completed),
                  'cost_cents': sum(j['cost_cents'] for j in completed),
                  'late': sum(j['finished_ms'] > j['deadline_ms'] for j in completed),
                  'unfinished_at_close': len(jobs)-len(completed),
                  'first_started': len(waits),
                  'max_first_wait_ms': max(waits, default=0),
                  'p95_first_wait_ms': waits[math.ceil(.95*len(waits))-1] if waits else 0}
        if any(run[k] != value for k, value in checks.items()):
            raise ValueError('independent metric recomputation failed')
        if not math.isclose(run['average_first_wait_ms'], sum(waits)/len(waits) if waits else 0, abs_tol=1e-6):
            raise ValueError('mean wait mismatch')
        observed_wait = sum((j['first_started_ms'] if j['first_started_ms'] is not None else report['horizon_ms'])-j['submitted_ms'] for j in jobs.values())/len(jobs)
        if not math.isclose(run['average_observed_wait_ms'], observed_wait, abs_tol=1e-6):
            raise ValueError('censored all-order wait mismatch')
        ends, dispatched = [0]*len(report['fleet']), set()
        for dispatch in run['dispatches']:
            j, p = jobs[dispatch['job_id']], report['fleet'][dispatch['printer']]
            if j['job_id'] in dispatched or dispatch['started_ms'] < max(j['submitted_ms'], ends[dispatch['printer']]):
                raise ValueError('duplicate, premature or overlapping dispatch')
            dispatched.add(j['job_id'])
            if j['color'] and not p['color'] or j['duplex'] and not p['duplex'] or j['paper'] == 'a3' and not p['a3']:
                raise ValueError('incompatible device')
            duration = p['color_page_ms' if j['color'] else 'mono_page_ms'] * j['pages'] * j['copies'] * (2 if j['paper'] == 'a3' else 1)
            if dispatch['finished_ms']-dispatch['started_ms'] != duration:
                raise ValueError('processing duration mismatch')
            ends[dispatch['printer']] = dispatch['finished_ms']
            if j['state'] == 'completed':
                cost = ((j['pages']+1)//2 if j['duplex'] else j['pages'])*j['copies']*(6 if j['paper'] == 'a3' else 3)
                cost += j['pages']*j['copies']*(45 if j['color'] else 7 if p['color'] else 5)*(2 if j['paper'] == 'a3' else 1)
                if j['cost_cents'] != cost or j['finished_ms'] != dispatch['finished_ms'] or j['finished_ms'] > report['horizon_ms']:
                    raise ValueError('settlement/cost mismatch')
        if any(j['job_id'] not in dispatched for j in jobs.values() if j['first_started_ms'] is not None):
            raise ValueError('started order missing dispatch evidence')
        busy = sum(min(d['finished_ms'], report['horizon_ms'])-d['started_ms'] for d in run['dispatches'])
        if not math.isclose(run['utilization'], busy/(report['horizon_ms']*len(report['fleet'])), abs_tol=1e-9):
            raise ValueError('utilization mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset', type=Path, required=True)
    parser.add_argument('--experiment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    pdf_log = logging.getLogger('pypdf')
    pdf_log.addHandler(logging.FileHandler(args.output/'pdf-read-warnings.log', encoding='utf-8'))
    pdf_log.propagate = False
    documents = extract(args.dataset, args.output)
    results, commands = [], []
    for scenario, hours in [('day', 8), ('peak', 1), ('batch', 1)]:
        source = workload(documents, scenario)
        input_path = args.output / f'{scenario}-workload.json'
        write_json(input_path, source)
        reports = []
        for repetition in range(2):
            output_path = args.output / f'{scenario}-result-{repetition+1}.json'
            command = [str(args.experiment.resolve()), '--input', str(input_path.resolve()),
                       '--hours', str(hours), '--output', str(output_path.resolve())]
            commands.append(command)
            subprocess.run(command, check=True, capture_output=True, timeout=180)
            reports.append(json.loads(output_path.read_text(encoding='utf-8')))
        if reports[0] != reports[1]:
            raise ValueError(f'{scenario}: repeat mismatch')
        if reports[0]['workload_sha256'] != sha256(input_path):
            raise ValueError('input hash mismatch')
        verify(reports[0], source)
        for run in reports[0]['runs']:
            results.append(dict(scenario=scenario, **{k: v for k, v in run.items() if k not in ('jobs', 'dispatches')}))
        print(f'{scenario}: all three policies, repeatability and independent evidence checks passed', flush=True)
    summary = {'files': len(documents), 'payload_bytes': sum(d['bytes'] for d in documents),
               'page_units': sum(d['pages'] for d in documents), 'raw_page_units': sum(d['raw_page_units'] for d in documents),
               'page_methods': dict(Counter(d['method'] for d in documents)),
               'page_confidence': dict(Counter(d['confidence'] for d in documents)),
               'clipped_files': sum(d['clipped'] for d in documents), 'runs': results,
               'verification': '9 policy/scenario comparisons, each repeated twice; independent dispatch, compatibility, cost, revenue, wait, express fulfillment and quality score checks passed'}
    write_json(args.output / 'summary.json', summary)
    manifest = {'dataset_manifest_sha256': sha256(args.dataset/'manifest.json'),
                'experiment_sha256': sha256(args.experiment), 'script_sha256': sha256(Path(__file__)),
                'python': sys.version, 'platform': platform.platform(), 'commands': commands,
                'outputs': {p.name: sha256(p) for p in args.output.iterdir() if p.is_file()}}
    write_json(args.output / 'run-manifest.json', manifest)
    print(json.dumps({k: v for k, v in summary.items() if k != 'runs'}, ensure_ascii=False, indent=2), flush=True)
    for run in results:
        print(f"{run['scenario']} / {run['policy']}: completed={run['completed']}, "
              f"mean_wait_min={run['average_first_wait_ms']/60000:.2f}, revenue={run['revenue_cents']/100:.2f}, "
              f"late={run['late']}", flush=True)


if __name__ == '__main__':
    main()
