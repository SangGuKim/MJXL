"""MJPEG 원본 보존 실험. Python 3 + PyAV + libjxl이 필요하다."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import zlib
import jpeg_coeff

MAGIC = b'MJXLLAB1'


class JXL:
    def __init__(self, dll_dir):
        self.directory = os.add_dll_directory(str(dll_dir))
        self.lib = C.CDLL(str(Path(dll_dir) / 'jxl.dll'))
        p, n, i = C.c_void_p, C.c_size_t, C.c_int
        signatures = {
            'JxlEncoderCreate': (p, [p]),
            'JxlEncoderDestroy': (None, [p]),
            'JxlEncoderUseContainer': (i, [p, i]),
            'JxlEncoderStoreJPEGMetadata': (i, [p, i]),
            'JxlEncoderFrameSettingsCreate': (p, [p, p]),
            'JxlEncoderAddJPEGFrame': (i, [p, p, n]),
            'JxlEncoderCloseInput': (None, [p]),
            'JxlEncoderProcessOutput': (i, [p, C.POINTER(p), C.POINTER(n)]),
            'JxlDecoderCreate': (p, [p]),
            'JxlDecoderDestroy': (None, [p]),
            'JxlDecoderSubscribeEvents': (i, [p, i]),
            'JxlDecoderSetInput': (i, [p, p, n]),
            'JxlDecoderCloseInput': (None, [p]),
            'JxlDecoderProcessInput': (i, [p]),
            'JxlDecoderSetJPEGBuffer': (i, [p, p, n]),
            'JxlDecoderReleaseJPEGBuffer': (n, [p]),
        }
        for name, (restype, argtypes) in signatures.items():
            f = getattr(self.lib, name)
            f.restype, f.argtypes = restype, argtypes

    def encode(self, data):
        lib = self.lib
        enc = lib.JxlEncoderCreate(None)
        if not enc:
            raise MemoryError('JXL encoder')
        try:
            self.check(lib.JxlEncoderUseContainer(enc, 1))
            self.check(lib.JxlEncoderStoreJPEGMetadata(enc, 1))
            settings = lib.JxlEncoderFrameSettingsCreate(enc, None)
            self.check(lib.JxlEncoderAddJPEGFrame(settings, data, len(data)))
            lib.JxlEncoderCloseInput(enc)
            parts = []
            while True:
                buf = C.create_string_buffer(65536)
                ptr = C.c_void_p(C.addressof(buf))
                size = C.c_size_t(len(buf))
                status = lib.JxlEncoderProcessOutput(enc, C.byref(ptr), C.byref(size))
                parts.append(buf.raw[:len(buf) - size.value])
                if status == 0:
                    return b''.join(parts)
                if status != 2:
                    raise ValueError(f'JXL encoding status {status}')
        finally:
            lib.JxlEncoderDestroy(enc)

    @staticmethod
    def check(status):
        if status != 0:
            raise ValueError(f'JXL API status {status}')

    def decode(self, data, original_size):
        lib = self.lib
        dec = lib.JxlDecoderCreate(None)
        if not dec:
            raise MemoryError('JXL decoder')
        try:
            self.check(lib.JxlDecoderSubscribeEvents(dec, 0x2000 | 0x1000))
            self.check(lib.JxlDecoderSetInput(dec, data, len(data)))
            lib.JxlDecoderCloseInput(dec)
            buf = None
            while True:
                status = lib.JxlDecoderProcessInput(dec)
                if status == 0x2000:
                    buf = C.create_string_buffer(original_size)
                    self.check(lib.JxlDecoderSetJPEGBuffer(dec, buf, len(buf)))
                elif status in (0, 0x1000):
                    if buf is None:
                        raise ValueError('JPEG reconstruction metadata missing')
                    unused = lib.JxlDecoderReleaseJPEGBuffer(dec)
                    if unused != 0:
                        raise ValueError('JPEG reconstruction size mismatch')
                    return buf.raw
                else:
                    raise ValueError(f'JXL decoding status {status}')
        finally:
            lib.JxlDecoderDestroy(dec)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compress(data, previous=b''):
    if previous:
        obj = zlib.compressobj(9, zdict=previous[-32768:])
        return obj.compress(data) + obj.flush()
    return zlib.compress(data, 9)


def decompress(data, previous=b''):
    if previous:
        obj = zlib.decompressobj(zdict=previous[-32768:])
        result = obj.decompress(data) + obj.flush()
        if not obj.eof or obj.unused_data:
            raise ValueError('Invalid dictionary payload')
        return result
    return zlib.decompress(data)


def restore(archive, jxl):
    with open(archive, 'rb') as f:
        if f.read(8) != MAGIC:
            raise ValueError('Invalid archive magic')
        header = json.loads(f.read(struct.unpack('<Q', f.read(8))[0]))
        chunks, previous = [], b''
        for record in header['records']:
            data = f.read(record['stored'])
            if len(data) != record['stored']:
                raise ValueError('Truncated payload')
            codec = record['codec']
            if codec == 'jxl':
                raw = jxl.decode(data, record['size'])
            elif codec == 'dict':
                raw = decompress(data, previous)
            elif codec == 'coeff':
                raw = jpeg_coeff.decode(data, previous)
            elif codec == 'zlib':
                raw = decompress(data)
            elif codec == 'raw':
                raw = data
            else:
                raise ValueError('Unknown codec')
            if len(raw) != record['size'] or digest(raw) != record['sha256']:
                raise ValueError('Record verification failed')
            chunks.append(raw)
            if record['video']:
                previous = raw
        if f.read(1):
            raise ValueError('Trailing archive data')
    result = b''.join(chunks)
    if len(result) != header['size'] or digest(result) != header['sha256']:
        raise ValueError('Original file verification failed')
    return result


def benchmark(source, output, jxl, gop):
    import av
    raw = source.read_bytes()
    with av.open(str(source)) as container:
        video = container.streams.video[0]
        if video.codec_context.name != 'mjpeg':
            raise ValueError('Only MJPEG sources are supported')
        streams = [{'type': s.type, 'codec': s.codec_context.name,
                    'time_base': str(s.time_base), 'duration': s.duration}
                   for s in container.streams]
        width, height = video.codec_context.width, video.codec_context.height
        packets = sorted([(p.pos, bytes(p)) for p in container.demux(video)
                          if p.size], key=lambda p: p[0] if p[0] is not None else -1)
    rows, segments, previous, cursor = [], [], b'', 0
    for index, (pos, data) in enumerate(packets):
        if pos is None or pos < cursor or raw[pos:pos + len(data)] != data:
            raise ValueError('Packet positions cannot reproduce original file')
        if pos > cursor:
            gap = raw[cursor:pos]
            segments.append((False, gap, {'zlib': compress(gap)}))
        start = time.perf_counter()
        encoded = jxl.encode(data)
        seconds = time.perf_counter() - start
        if jxl.decode(encoded, len(data)) != data:
            raise ValueError('JPEG round-trip failed')
        candidates = {'jxl': encoded}
        if index % gop != 0:
            candidates['dict'] = compress(data, previous)
            try:
                candidate = jpeg_coeff.encode(data, previous)
                if jpeg_coeff.decode(candidate, previous) != data:
                    raise ValueError('Coefficient round-trip failed')
                candidates['coeff'] = candidate
            except (ValueError, IndexError, KeyError) as error:
                print(f'Frame {index}: coefficient fallback: {error}', flush=True)
        rows.append({'frame': index, 'original': len(data), 'jxl': len(encoded),
                     'dictionary': len(candidates.get('dict', encoded)),
                     'coefficient': len(candidates['coeff']) if 'coeff' in candidates else None,
                     'jxl_seconds': seconds})
        segments.append((True, data, candidates))
        previous, cursor = data, pos + len(data)
        if (index + 1) % 12 == 0 or index + 1 == len(packets):
            print(f'{source.name}: encoded and verified {index + 1}/{len(packets)} frames', flush=True)
    if cursor < len(raw):
        gap = raw[cursor:]
        segments.append((False, gap, {'zlib': compress(gap)}))
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for mode in ('jxl', 'inter', 'coeff', 'hybrid'):
        records, payloads = [], []
        for is_video, original, candidates in segments:
            if not is_video:
                codec = 'zlib'
            elif mode == 'jxl':
                codec = 'jxl'
            elif mode == 'inter':
                codec = 'dict' if 'dict' in candidates else 'jxl'
            elif mode == 'coeff':
                codec = 'coeff' if 'coeff' in candidates else 'jxl'
            else:
                codec = min(candidates, key=lambda k: len(candidates[k]))
            payload = candidates[codec]
            records.append({'video': is_video, 'codec': codec, 'size': len(original),
                            'stored': len(payload), 'sha256': digest(original)})
            payloads.append(payload)
        header = json.dumps({'version': 1, 'name': source.name, 'size': len(raw),
                             'sha256': digest(raw), 'gop': gop,
                             'records': records}, separators=(',', ':')).encode()
        path = output / f'{source.stem}.{mode}.mjxl-lab'
        with path.open('xb') as f:
            f.write(MAGIC + struct.pack('<Q', len(header)) + header)
            for payload in payloads:
                f.write(payload)
        if restore(path, jxl) != raw:
            raise ValueError('Archive round-trip failed')
        print(f'{source.name}: {mode} archive round-trip verified', flush=True)
        size = path.stat().st_size
        results.append({'mode': mode, 'archive_bytes': size,
                        'saved_percent': 100 * (1 - size / len(raw)),
                        'video_bytes': sum(r['stored'] for r in records if r['video']),
                        'dict_frames': sum(r['codec'] == 'dict' for r in records),
                        'coeff_frames': sum(r['codec'] == 'coeff' for r in records),
                        'verified': True})
    report = {'source': str(source), 'sha256': digest(raw), 'original_bytes': len(raw),
              'width': width, 'height': height, 'streams': streams, 'gop': gop,
              'frames': rows, 'results': results,
              'limitation': 'Dictionary baseline uses previous JPEG last 32 KiB. Coefficient P uses same-position quantized DCT residuals and original Huffman symbols, DEFLATE, no motion compensation. Baseline single-scan JPEG only; otherwise JXL fallback. Experimental archive, not a playable container.'}
    report_path = output / f'{source.stem}.report.json'
    with report_path.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write('\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'frames'}, indent=2, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll-dir', type=Path, required=True)
    sub = parser.add_subparsers(dest='command', required=True)
    bench = sub.add_parser('bench')
    bench.add_argument('source', type=Path)
    bench.add_argument('output', type=Path)
    bench.add_argument('--gop', type=int, default=12)
    unpack = sub.add_parser('restore')
    unpack.add_argument('archive', type=Path)
    unpack.add_argument('output', type=Path)
    args = parser.parse_args()
    jxl = JXL(args.dll_dir)
    if args.command == 'bench':
        if args.gop < 1:
            parser.error('--gop must be positive')
        benchmark(args.source, args.output, jxl, args.gop)
    else:
        data = restore(args.archive, jxl)
        with args.output.open('xb') as f:
            f.write(data)
        print(f'Verified SHA-256: {digest(data)}')


if __name__ == '__main__':
    main()
