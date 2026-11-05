"""독립 CLI의 PCM·FLAC 변환과 원본 전체 복원을 플랫폼 공통으로 검증한다."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compatible-avi', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    sources = sorted(p for p in args.samples.iterdir() if p.suffix.lower() == '.mov')
    if not sources:
        parser.error('No MOV samples found')
    args.output.mkdir(parents=True, exist_ok=False)

    def run(*arguments):
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, encoding='utf-8', errors='replace')
        if result.returncode:
            raise RuntimeError(f'{arguments}: {result.stdout}\n{result.stderr}')
        return result.stdout.strip()

    records = []
    for source in sources:
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        for mode in ('pcm', 'flac'):
            folder = args.output / mode
            folder.mkdir(exist_ok=True)
            avi = folder / (source.stem + '.avi')
            restored = folder / (source.stem + '.restored.mov')
            run('encode', source, avi, *(['--flac'] if mode == 'flac' else []))
            verification = run('verify', source, avi)
            run('restore-original', avi, restored)
            if hashlib.sha256(restored.read_bytes()).hexdigest() != digest:
                raise RuntimeError(f'Original hash differs: {source} ({mode})')
            # 비교용 복원 파일은 검증 후 삭제하고 압축 결과와 보고서를 남긴다.
            restored.unlink()
            records.append(dict(source=source.name, mode=mode, original_bytes=source.stat().st_size,
                                avi_bytes=avi.stat().st_size, sha256=digest, verification=verification))
            print(f'PASS {mode}: {source.name}', flush=True)
    if args.compatible_avi:
        verification = run('verify', sources[0], args.compatible_avi)
        restored = args.output / 'compatible.restored.mov'
        run('restore-original', args.compatible_avi, restored)
        if restored.read_bytes() != sources[0].read_bytes():
            raise RuntimeError('Compatible AVI restored bytes differ')
        restored.unlink()
        print(f'PASS compatible AVI: {args.compatible_avi}', flush=True)
    run('restore', args.output / 'flac' / (sources[0].stem + '.avi'), args.output / 'restored-mjpeg.avi')
    print(run('bench', args.output / 'flac' / (sources[0].stem + '.avi')), flush=True)
    (args.output / 'cli-report.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(records)} encodes and exact original restores', flush=True)


if __name__ == '__main__':
    main()
