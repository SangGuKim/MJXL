"""샘플 전체의 실제 인코더, 원본 복원과 DirectShow 필터를 검증한다."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
binary = root / 'dist' / 'Release'
# 기본은 PCM 저장을 검증하고 --flac을 주면 FLAC 저장을 검증한다.
use_flac = '--flac' in sys.argv[1:]
output = root / 'results' / ('native-original-flac' if use_flac else 'native-original')
output.mkdir(parents=True, exist_ok=True)
results = []
for source in sorted((root / 'samples').glob('*.MOV')):
    avi = output / (source.stem + '.avi')
    command = ['verify', source, avi] if avi.exists() else ['encode', source, avi, *(['--flac'] if use_flac else [])]
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
    restored = output / (source.stem + '.restored.mov')
    rebuild = subprocess.run([str(binary / 'mjxl.exe'), 'restore-original', str(avi), str(restored)], capture_output=True, encoding='utf-8', errors='replace')
    if rebuild.returncode:
        print(rebuild.stderr, file=sys.stderr)
        raise SystemExit(rebuild.returncode)
    # 인코더의 자체 검사와 별개로 복원 파일을 원본과 해시로 비교한다.
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    restored_hash = hashlib.sha256(restored.read_bytes()).hexdigest()
    restored.unlink()
    if original_hash != restored_hash:
        print(f'Restored file differs: {source.name}', file=sys.stderr)
        raise SystemExit(1)
    print(f'Restored exact original: {source.name} {original_hash}', flush=True)
    results.append({'source': source.name, 'original_bytes': source.stat().st_size,
                    'avi_bytes': avi.stat().st_size, 'verification': completed.stdout.strip(),
                    'directshow': graph.stdout.strip(), 'original_sha256': original_hash, 'verified': True})
with (output / 'native-report.json').open('w', encoding='utf-8', newline='\n') as f:
    json.dump(results, f, indent=2, ensure_ascii=False)
    f.write('\n')
print(f'PASS all {len(results)} native encodes, JPEG/PCM checks, exact original restores, DirectShow playback and seeking')
