"""Summarize verified sample compression results and pixel-decoding benchmarks."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    reports = sorted((args.root / 'results' / 'coeff-v1').glob('*.report.json'))
    reports += sorted((args.root / 'results' / 'batch-v1').glob('*/*.report.json'))
    documents = [json.loads(path.read_text(encoding='utf-8')) for path in reports]
    total_original = 0
    totals = {mode: 0 for mode in ('jxl', 'inter', 'coeff', 'hybrid')}
    lines = ['# Sample comparison', '',
             'Stored sizes include original audio, container preservation data, and experimental archive headers. Negative savings mean a larger file.', '',
             '| Video | Frames | Original bytes | JXL savings | Coefficient P size / original | Hybrid P frames |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    for doc in documents:
        results = {row['mode']: row for row in doc['results']}
        if not all(row['verified'] for row in results.values()):
            raise ValueError('Unverified result')
        total_original += doc['original_bytes']
        for mode in totals:
            totals[mode] += results[mode]['archive_bytes']
        mixed = results['hybrid']
        p_count = mixed['dict_frames'] + mixed['coeff_frames']
        coeff_ratio = f"{results['coeff']['archive_bytes'] / doc['original_bytes']:.2f}x"
        if results['coeff']['coeff_frames'] == 0:
            coeff_ratio = 'Unsupported (JXL fallback)'
        lines.append(f"| {Path(doc['source']).name} | {len(doc['frames'])} | {doc['original_bytes']:,} | {results['jxl']['saved_percent']:.2f}% | {coeff_ratio} | {p_count} |")
    lines += ['', '## Totals', '', f'Verified {len(documents)} source files totaling {total_original:,} bytes.', '',
              '| Mode | Stored bytes | Savings |', '| --- | ---: | ---: |']
    for mode, size in totals.items():
        percent = 100 * (1 - size / total_original) if total_original else 0
        lines.append(f'| {mode} | {size:,} | {percent:.2f}% |')
    lines += ['', 'All results passed full-file byte comparison and SHA-256 verification. Coefficient P is an initial baseline without motion compensation. Its totals include JXL fallback for unsupported inputs and do not measure pure P-frame performance or the limits of reversible inter-frame compression.', '',
              '## Pixel-decoding throughput', '',
              'Inputs were decoded from memory to RGB8 after warmup. Throughput is the median of three runs. Disk access, audio, rendering, and restoration filters are excluded. JXL workers count libjxl workers, not all process threads. Concurrent measurements ran alongside compression; isolated measurements ran after compression completed.', '',
              '| Source | Conditions | Source fps | Decoder | Workers | Decode fps | p95 ms | Maximum ms |',
              '| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |']
    for path in sorted((args.root / 'results').glob('decode-*.json')):
        doc = json.loads(path.read_text(encoding='utf-8'))
        condition = 'Concurrent compression' if doc.get('concurrent_compression', True) else 'After compression'
        for row in doc['results']:
            workers = row.get('jxl_workers')
            workers = 'Single' if workers in (None, 0) else str(workers)
            p95, maximum = row.get('p95_milliseconds'), row.get('max_milliseconds')
            p95 = f'{p95:.2f}' if p95 is not None else 'Not measured'
            maximum = f'{maximum:.2f}' if maximum is not None else 'Not measured'
            lines.append(f"| {Path(doc['source']).name} | {condition} | {doc['source_fps']:g} | {row['codec']} | {workers} | {row['fps_median']:.1f} | {p95} | {maximum} |")
    (args.root / 'COMPARISON.md').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'verified_sources': len(documents), 'original_bytes': total_original,
                      'archive_bytes': totals}, indent=2))


if __name__ == '__main__':
    main()
