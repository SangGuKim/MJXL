"""Validate standalone CLI PCM/FLAC encoding and exact original-file restoration across platforms."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compatible-avi', type=Path)
    parser.add_argument('--effort', type=int, choices=range(1, 11), default=7)
    parser.add_argument('--container', choices=('avi', 'mkv'), default='avi')
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
            encoded = folder / (source.stem + '.' + args.container)
            restored = folder / (source.stem + '.restored.mov')
            summary = run('encode', source, encoded, '--effort', args.effort,
                          '--audio-codec', 'copy' if mode == 'pcm' else 'flac')
            components = {}
            for label in ('Audio', 'Video', 'Recovery payload', 'Container', 'Total file'):
                match = re.search(r'^' + re.escape(label) + r'\s+([\d,]+)\s+->\s+([\d,]+)', summary, re.MULTILINE)
                if not match:
                    raise RuntimeError(f'Missing size component {label}: {summary}')
                components[label] = [int(value.replace(',', '')) for value in match.groups()]
            if any(sum(components[label][side] for label in ('Audio', 'Video', 'Recovery payload', 'Container'))
                   != components['Total file'][side] for side in (0, 1)):
                raise RuntimeError(f'Component totals differ: {components}')
            if components['Total file'] != [source.stat().st_size, encoded.stat().st_size]:
                raise RuntimeError('Reported file sizes differ from disk')
            verification = run('verify', source, encoded)
            run('restore-original', encoded, restored)
            if hashlib.sha256(restored.read_bytes()).hexdigest() != digest:
                raise RuntimeError(f'Original hash differs: {source} ({mode})')
            # Remove comparison restore files after verification; retain encoded files and reports.
            restored.unlink()
            records.append(dict(source=source.name, mode=mode, original_bytes=source.stat().st_size,
                                container=args.container, components=components, effort=args.effort, encoded_bytes=encoded.stat().st_size, sha256=digest, verification=verification))
            print(f'PASS {mode}: {source.name}', flush=True)
    if args.compatible_avi:
        verification = run('verify', sources[0], args.compatible_avi)
        restored = args.output / 'compatible.restored.mov'
        run('restore-original', args.compatible_avi, restored)
        if restored.read_bytes() != sources[0].read_bytes():
            raise RuntimeError('Compatible AVI restored bytes differ')
        restored.unlink()
        print(f'PASS compatible AVI: {args.compatible_avi}', flush=True)
    run('restore', args.output / 'flac' / (sources[0].stem + '.' + args.container), args.output / 'restored-mjpeg.avi')
    print(run('bench', args.output / 'flac' / (sources[0].stem + '.' + args.container)), flush=True)
    (args.output / 'cli-report.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(records)} encodes and exact original restores', flush=True)


if __name__ == '__main__':
    main()
