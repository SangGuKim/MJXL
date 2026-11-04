"""검증된 샘플별 압축 결과와 픽셀 디코딩 측정값을 문서로 집계한다."""
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
    lines = ['# 전체 샘플 비교', '',
             '저장 크기는 원본 음성·컨테이너 정보 보존 및 실험 아카이브 헤더를 포함한다. 절감률이 음수이면 원본보다 커진 것이다.', '',
             '| 영상 | 프레임 | 원본(바이트) | JXL 절감률 | 계수 P 크기(원본 대비) | 혼합 P 선택 수 |',
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
        coeff_ratio = f"{results['coeff']['archive_bytes'] / doc['original_bytes']:.2f}배"
        if results['coeff']['coeff_frames'] == 0:
            coeff_ratio = '미지원(JXL 대체)'
        lines.append(f"| {Path(doc['source']).name} | {len(doc['frames'])} | {doc['original_bytes']:,} | {results['jxl']['saved_percent']:.2f}% | {coeff_ratio} | {p_count} |")
    lines += ['', '## 합계', '', f'검증된 원본 파일은 {len(documents)}개이며 합계 {total_original:,}바이트이다.', '',
              '| 방식 | 저장 크기(바이트) | 절감률 |', '| --- | ---: | ---: |']
    for mode, size in totals.items():
        percent = 100 * (1 - size / total_original) if total_original else 0
        lines.append(f'| {mode} | {size:,} | {percent:.2f}% |')
    lines += ['', '모든 표의 결과는 파일 전체 바이트 비교 및 SHA-256 검증을 통과했다. 계수 P는 움직임 보상이 없는 초기 기준선이다. 미지원 입력의 JXL 대체 결과를 합계에 포함하므로 계수 P 합계는 순수한 P프레임 성능이 아니다. 일반적인 프레임 간 가역 압축의 최적 성능을 나타내지 않는다.', '',
              '## 픽셀 디코딩 속도', '',
              '예열 후 메모리 내 입력을 RGB8 픽셀로 디코딩한 처리량이다. 3회 실행의 전체 처리량 중앙값을 사용한다. 디스크·음성·화면 출력·화질 보정은 제외한다. JXL 작업 스레드는 libjxl worker 수이며 전체 프로세스 스레드 수가 아니다. 동시 압축 행은 압축 실험과 동시 실행한 예비 측정값이다. 압축 종료 후 행은 압축 비교가 모두 끝난 뒤 측정했다.', '',
              '| 측정 파일 | 조건 | 원본 fps | 디코더 | 작업 스레드 | 디코딩 fps | p95 지연(ms) | 최대 지연(ms) |',
              '| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |']
    for path in sorted((args.root / 'results').glob('decode-*.json')):
        doc = json.loads(path.read_text(encoding='utf-8'))
        condition = '동시 압축' if doc.get('concurrent_compression', True) else '압축 종료 후'
        for row in doc['results']:
            workers = row.get('jxl_workers')
            workers = '단일' if workers in (None, 0) else str(workers)
            p95, maximum = row.get('p95_milliseconds'), row.get('max_milliseconds')
            p95 = f'{p95:.2f}' if p95 is not None else '미측정'
            maximum = f'{maximum:.2f}' if maximum is not None else '미측정'
            lines.append(f"| {Path(doc['source']).name} | {condition} | {doc['source_fps']:g} | {row['codec']} | {workers} | {row['fps_median']:.1f} | {p95} | {maximum} |")
    (args.root / 'COMPARISON.md').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'verified_sources': len(documents), 'original_bytes': total_original,
                      'archive_bytes': totals}, indent=2))


if __name__ == '__main__':
    main()
