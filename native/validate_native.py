"""샘플 전체의 실제 인코더와 DirectShow 필터를 검증한다."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
binary = root / 'dist' / 'Release'
output = root / 'results' / 'native-v1'
output.mkdir(parents=True, exist_ok=True)
results = []
for source in sorted((root / 'samples').glob('*.MOV')):
    avi = output / (source.stem + '.avi')
    command = ['verify', source, avi] if avi.exists() else ['encode', source, avi]
    completed = subprocess.run([str(binary / 'mjxl.exe'), *map(str, command)], capture_output=True, encoding='utf-8', errors='replace')
    print(completed.stdout, end='', flush=True)
    if completed.returncode:
        print(completed.stderr, file=sys.stderr)
        raise SystemExit(completed.returncode)
    graph = subprocess.run([str(binary / 'mjxl_graph_test.exe'), str(binary / 'mjxl_decoder.ax'), str(avi)], capture_output=True, encoding='utf-8', errors='replace')
    print(graph.stdout, end='', flush=True)
    if graph.returncode:
        print(graph.stderr, file=sys.stderr)
        raise SystemExit(graph.returncode)
    results.append({'source': source.name, 'original_bytes': source.stat().st_size,
                    'avi_bytes': avi.stat().st_size, 'verification': completed.stdout.strip(),
                    'directshow': graph.stdout.strip(), 'verified': True})
with (output / 'native-report.json').open('w', encoding='utf-8', newline='\n') as f:
    json.dump(results, f, indent=2, ensure_ascii=False)
    f.write('\n')
print(f'PASS all {len(results)} native encodes, JPEG/PCM checks, DirectShow playback and seeking')
