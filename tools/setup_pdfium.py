"""Install the pinned native PDF renderer into this checkout, never globally."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
target = root / '.tools' / 'pdfium'
subprocess.run([sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check',
                '--no-deps', '--target', str(target), 'pypdfium2==5.13.0'], check=True)
print(f'PDFium and its redistribution licenses installed in {target}')
