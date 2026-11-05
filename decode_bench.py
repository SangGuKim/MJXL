"""Benchmark in-memory MJPEG and JXL decoding to RGB8 pixels."""
import argparse
import ctypes as C
import json
from pathlib import Path
import statistics
import struct
import time
import av
from mjxl_lab import JXL, MAGIC


class PixelFormat(C.Structure):
    _fields_ = [('channels', C.c_uint32), ('type', C.c_int),
                ('endianness', C.c_int), ('align', C.c_size_t)]


class Pixels:
    def __init__(self, jxl, dll_dir, workers):
        self.jxl, self.lib = jxl, jxl.lib
        self.fmt = PixelFormat(3, 2, 0, 0)
        p, n, i = C.c_void_p, C.c_size_t, C.c_int
        for name, restype, args in [
            ('JxlDecoderImageOutBufferSize', i, [p, C.POINTER(PixelFormat), C.POINTER(n)]),
            ('JxlDecoderSetImageOutBuffer', i, [p, C.POINTER(PixelFormat), p, n]),
            ('JxlDecoderSetParallelRunner', i, [p, p, p]),
        ]:
            fn = getattr(self.lib, name)
            fn.restype, fn.argtypes = restype, args
        self.threads = C.CDLL(str(dll_dir / 'jxl_threads.dll'))
        self.threads.JxlThreadParallelRunnerCreate.restype = p
        self.threads.JxlThreadParallelRunnerCreate.argtypes = [p, n]
        self.threads.JxlThreadParallelRunnerDestroy.restype = None
        self.threads.JxlThreadParallelRunnerDestroy.argtypes = [p]
        self.runner = self.threads.JxlThreadParallelRunnerCreate(None, workers) if workers else None
        if workers and not self.runner:
            raise MemoryError('JXL worker pool')

    def close(self):
        if self.runner:
            self.threads.JxlThreadParallelRunnerDestroy(self.runner)
            self.runner = None

    def decode(self, data):
        lib = self.lib
        dec = lib.JxlDecoderCreate(None)
        if not dec:
            raise MemoryError('JXL decoder')
        try:
            if self.runner:
                self.jxl.check(lib.JxlDecoderSetParallelRunner(
                    dec, C.cast(self.threads.JxlThreadParallelRunner, C.c_void_p), self.runner))
            self.jxl.check(lib.JxlDecoderSubscribeEvents(dec, 0x1000))
            self.jxl.check(lib.JxlDecoderSetInput(dec, data, len(data)))
            lib.JxlDecoderCloseInput(dec)
            buf = None
            while True:
                status = lib.JxlDecoderProcessInput(dec)
                if status == 5:
                    size = C.c_size_t()
                    self.jxl.check(lib.JxlDecoderImageOutBufferSize(dec, C.byref(self.fmt), C.byref(size)))
                    buf = C.create_string_buffer(size.value)
                    self.jxl.check(lib.JxlDecoderSetImageOutBuffer(dec, C.byref(self.fmt), buf, len(buf)))
                elif status == 0x1000:
                    if buf is None:
                        raise ValueError('No pixel buffer')
                    return len(buf)
                else:
                    raise ValueError(f'Pixel decoding status {status}')
        finally:
            lib.JxlDecoderDestroy(dec)


def read_jxl(path):
    packets = []
    with path.open('rb') as f:
        if f.read(8) != MAGIC:
            raise ValueError('Invalid archive')
        header = json.loads(f.read(struct.unpack('<Q', f.read(8))[0]))
        for rec in header['records']:
            data = f.read(rec['stored'])
            if rec['video']:
                if rec['codec'] != 'jxl':
                    raise ValueError('Benchmark requires independent JXL archive')
                packets.append(data)
    return packets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dll-dir', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--contended', action='store_true')
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    with av.open(str(args.source)) as c:
        stream = c.streams.video[0]
        width, height = stream.codec_context.width, stream.codec_context.height
        fps = float(stream.average_rate)
        original = [bytes(p) for p in c.demux(stream) if p.size]
    jxl = JXL(args.dll_dir)
    if str(args.archive) == '-':
        compressed = [jxl.encode(packet) for packet in original]
    else:
        compressed = read_jxl(args.archive)
    if len(original) != len(compressed):
        raise ValueError('Frame count mismatch')
    for raw, encoded in zip(original, compressed):
        if jxl.decode(encoded, len(raw)) != raw:
            raise ValueError('JXL input does not match source JPEG')
    results = []
    for kind, workers in [('mjpeg', 0), ('jxl', 0), ('jxl', 4)]:
        decoder = Pixels(jxl, args.dll_dir, workers) if kind == 'jxl' else av.CodecContext.create('mjpeg', 'r')
        if kind == 'mjpeg':
            decoder.thread_count = 1
        def run():
            packets = compressed if kind == 'jxl' else original
            latencies = []
            for data in packets:
                frame_start = time.perf_counter()
                if kind == 'jxl':
                    size = decoder.decode(data)
                else:
                    frames = decoder.decode(av.Packet(data))
                    if len(frames) != 1:
                        raise ValueError('Expected one decoded frame')
                    size = frames[0].to_ndarray(format='rgb24').nbytes
                if size != width * height * 3:
                    raise ValueError('Unexpected pixel size')
                latencies.append((time.perf_counter() - frame_start) * 1000)
            return latencies
        try:
            run()
            elapsed = []
            latencies = []
            for _ in range(args.repeats):
                start = time.perf_counter()
                latencies.extend(run())
                elapsed.append(time.perf_counter() - start)
            median = statistics.median(elapsed)
            row = {'codec': kind, 'jxl_workers': workers if kind == 'jxl' else None,
                   'seconds': elapsed, 'fps_median': len(original) / median,
                   'milliseconds_per_frame': median * 1000 / len(original),
                   'p95_milliseconds': sorted(latencies)[int(0.95 * (len(latencies) - 1))],
                   'max_milliseconds': max(latencies),
                   'realtime_multiple': len(original) / median / fps}
            results.append(row)
            print(json.dumps(row), flush=True)
        finally:
            if kind == 'jxl':
                decoder.close()
    report = {'source': str(args.source), 'frames': len(original), 'source_fps': fps,
              'width': width, 'height': height, 'repeats': args.repeats,
              'concurrent_compression': args.contended,
              'results': results,
              'scope': 'Warm in-memory RGB8 decoding; includes per-frame decoder creation for independent JXL, excludes disk, renderer, audio, postprocessing.'}
    with args.output.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(report, f, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()
