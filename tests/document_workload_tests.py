"""Document metadata mapping and three-policy replay using self-owned fixtures."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_document_experiments import extract, sha256, verify, workload


def require(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--experiment', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='大学 文档负载-') as temporary:
        root = Path(temporary); dataset = root/'数据集'; dataset.mkdir()
        output = root/'结果'; output.mkdir()
        docs = [
            ('私有原文件名.docx', {'docProps/app.xml': '<Properties xmlns="urn:test"><Pages>3</Pages></Properties>'}),
            ('presentation.pptx', {'ppt/presentation.xml': '<presentation xmlns="urn:test"><sldIdLst><sldId/><sldId/><sldId/><sldId/><sldId/></sldIdLst></presentation>'}),
            ('sheet.xlsx', {'xl/worksheets/sheet1.xml': '<worksheet xmlns="urn:test"><sheetData>' + '<row><c/></row>'*100 + '</sheetData></worksheet>'}),
        ]
        manifest = []
        for name, entries in docs:
            path = dataset/name
            with ZipFile(path, 'w') as archive:
                for member, data in entries.items():
                    archive.writestr(member, data)
            manifest.append({'relative_path': name, 'bytes': path.stat().st_size,
                             'extension': path.suffix, 'sha256': sha256(path)})
        table = dataset/'table.csv'; table.write_text('a,b\n'*50, encoding='utf-8')
        manifest.append({'relative_path': table.name, 'bytes': table.stat().st_size,
                         'extension': '.csv', 'sha256': sha256(table)})
        (dataset/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
        measured = extract(dataset, output)
        counts = {d['extension']: d['pages'] for d in measured}
        require(counts == {'.docx': 3, '.pptx': 5, '.xlsx': 3, '.csv': 2}, 'fixture page/slide/table counts')
        require({d['confidence'] for d in measured} == {'cached_pages', 'exact_slides', 'estimated_pages'}, 'confidence labels')
        source = workload(measured, 'batch')
        require('私有原文件名' not in json.dumps(source, ensure_ascii=False) and temporary not in json.dumps(source), 'workload must not disclose source names or paths')
        require(source == workload(measured, 'batch'), 'deterministic mapping')
        input_path = root/'input.json'; input_path.write_text(json.dumps(source), encoding='utf-8')
        reports = []
        for repeat in range(2):
            path = root/f'replay-{repeat}.json'
            subprocess.run([args.experiment, '--input', str(input_path), '--hours', '1', '--output', str(path)],
                           check=True, capture_output=True, timeout=20)
            reports.append(json.loads(path.read_text(encoding='utf-8')))
        require(reports[0] == reports[1], 'three-policy repeatability')
        require(reports[0]['workload_sha256'] == sha256(input_path), 'input provenance hash')
        verify(reports[0], source)
        damaged = copy.deepcopy(reports[0]); damaged['runs'][0]['dispatches'][0]['finished_ms'] += 1
        try:
            verify(damaged, source)
        except ValueError:
            pass
        else:
            raise RuntimeError('independent verifier must reject corrupted dispatch evidence')
        damaged = copy.deepcopy(reports[0]); damaged['runs'][0]['quality_score'] += 1
        try:
            verify(damaged, source)
        except ValueError:
            pass
        else:
            raise RuntimeError('independent verifier must reject a fabricated quality score')
        # Bad/duplicate input must fail rather than produce a partial comparison.
        for replacement in (dict(source, jobs=[source['jobs'][0], source['jobs'][0]]),
                            dict(source, jobs=[dict(source['jobs'][0], pages=1001)])):
            input_path.write_text(json.dumps(replacement), encoding='utf-8')
            result = subprocess.run([args.experiment, '--input', str(input_path), '--output', str(root/'invalid.json')],
                                    capture_output=True, timeout=20)
            require(result.returncode == 64 and not (root/'invalid.json').exists(), 'invalid workload rejected atomically')
        table.write_text('changed', encoding='utf-8')
        try:
            extract(dataset, output)
        except ValueError:
            pass
        else:
            raise RuntimeError('source hash mismatch must stop extraction')
        print('Document metadata, anonymized mapping, three policies, independent checks and bad-input rejection passed.')


if __name__ == '__main__':
    main()
