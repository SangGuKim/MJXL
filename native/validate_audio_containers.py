"""Compare lossless audio codecs and containers, including exact archive restoration."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--ffmpeg', required=True, type=Path)
    parser.add_argument('--ffprobe', required=True, type=Path)
    parser.add_argument('--samples', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--effort', type=int, choices=range(1, 11), default=1)
    args = parser.parse_args()
    binary, ffmpeg, ffprobe = (p.resolve() for p in (args.binary, args.ffmpeg, args.ffprobe))
    sources = sorted(p for p in args.samples.iterdir() if p.suffix.lower() in ('.mov', '.avi'))
    if not sources:
        parser.error('No MOV/AVI inputs found')
    args.output.mkdir(parents=True, exist_ok=False)
    records = []

    def invoke(arguments, required=True):
        result = subprocess.run(list(map(str, arguments)), capture_output=True)
        if required and result.returncode:
            raise RuntimeError(f'{arguments}: {result.stderr.decode("utf-8", errors="replace")}')
        return result

    def cli(*arguments):
        return invoke([binary, *arguments]).stdout.decode('utf-8')

    def probe(path):
        return json.loads(invoke([ffprobe, '-v', 'error', '-show_streams', '-of', 'json', path]).stdout)['streams']

    def pcm(path, forced=False):
        return invoke([ffmpeg, '-v', 'error', *(['-c:a', 'alac'] if forced else []), '-i', path,
                       '-map', '0:a:0', '-f', 's32le', 'pipe:1'])

    try:
        for source in sources:
            streams = probe(source)
            audio = next((s for s in streams if s['codec_type'] == 'audio'), None)
            expected_pcm = pcm(source).stdout if audio else b''
            original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
            payloads = {}
            cases = [('avi', 'copy'), ('avi', 'flac'), ('avi', 'alac'),
                     ('mkv', 'copy'), ('mkv', 'flac'), ('mkv', 'alac'), ('mov', 'copy'), ('mov', 'alac')]
            if not audio:
                cases = [(c, 'copy') for c in ('avi', 'mkv', 'mov')]
            for container, codec in cases:
                experiment = container == 'avi' and codec == 'alac'
                folder = args.output / container / codec
                folder.mkdir(parents=True, exist_ok=True)
                encoded = folder / f'{source.stem}.{container}'
                restored = folder / f'{source.stem}.restored-original'
                options = ['--effort', args.effort]
                # Exercise MOV's default selection rather than only an explicit ALAC request.
                if not (container == 'mov' and codec == 'alac'):
                    options += ['--audio-codec', codec]
                if experiment:
                    options += ['--experimental-avi-alac']
                if codec == 'flac' and audio and max(int(audio.get('bits_per_sample', 0) or 0),
                                                                    int(audio.get('bits_per_raw_sample', 0) or 0)) == 32:
                    rejected = invoke([binary, 'encode', source, encoded, *options], required=False)
                    if rejected.returncode == 0 or encoded.exists() or Path(str(encoded) + '.partial').exists():
                        raise RuntimeError('Explicit unsupported PCM32 FLAC request was not rejected cleanly')
                    print(f'PASS {source.name} {container}/flac: explicit PCM32 refusal', flush=True)
                    continue
                summary = cli('encode', source, encoded, *options)
                components = {}
                for label in ('Audio', 'Video', 'Recovery payload', 'Container', 'Total file'):
                    match = re.search(r'^' + re.escape(label) + r'\s+([\d,]+)\s+->\s+([\d,]+)', summary, re.MULTILINE)
                    if not match:
                        raise RuntimeError(f'Missing component {label}')
                    components[label] = [int(v.replace(',', '')) for v in match.groups()]
                if any(sum(components[k][side] for k in ('Audio', 'Video', 'Recovery payload', 'Container'))
                       != components['Total file'][side] for side in (0, 1)):
                    raise RuntimeError('Component totals differ')
                cli('verify', source, encoded)
                cli('restore-original', encoded, restored)
                if hashlib.sha256(restored.read_bytes()).hexdigest() != original_hash:
                    raise RuntimeError(f'Original hash mismatch: {encoded}')
                restored.unlink()
                output_streams = probe(encoded)
                output_audio = next((s for s in output_streams if s['codec_type'] == 'audio'), None)
                expected_codec = 'pcm_' if codec == 'copy' else codec
                if audio and not experiment and not output_audio.get('codec_name', '').startswith(expected_codec):
                    raise RuntimeError(f'Unexpected audio codec: {output_audio}')
                external = invoke([ffmpeg, '-v', 'error', '-xerror', '-c:v', 'libjxl', '-i', encoded,
                                   '-map', '0:v:0', '-map', '0:a:0?', '-f', 'null', '-'], required=False)
                if not experiment and external.returncode:
                    raise RuntimeError(external.stderr.decode('utf-8', errors='replace'))
                forced_audio = False
                if audio:
                    decoded = invoke([ffmpeg, '-v', 'error', '-i', encoded, '-map', '0:a:0', '-f', 's32le', 'pipe:1'], required=False)
                    if experiment and decoded.returncode:
                        decoded = pcm(encoded, forced=True)
                        forced_audio = True
                    if decoded.returncode or decoded.stdout != expected_pcm:
                        raise RuntimeError(f'External PCM mismatch: {encoded}')
                digests = []
                for track in ('v', 'a') if audio else ('v',):
                    data = invoke([ffmpeg, '-v', 'error', '-c:v', 'libjxl', *(['-c:a', 'alac'] if experiment else []),
                                   '-i', encoded, '-map', f'0:{track}:0', '-c', 'copy', '-f', 'data', 'pipe:1']).stdout
                    digests.append(hashlib.sha256(data).hexdigest())
                key = codec
                if key in payloads and payloads[key] != digests:
                    raise RuntimeError(f'Cross-container media payload mismatch: {source.name}, {codec}')
                payloads[key] = digests
                records.append(dict(source=source.name, source_sha256=original_hash, container=container, codec=codec,
                                    effort=args.effort, components=components, external_auto_decode=external.returncode == 0,
                                    external_forced_alac=forced_audio, audio_stream=output_audio,
                                    media_sha256=digests, output=str(encoded)))
                print(f'PASS {source.name} {container}/{codec}: audio={components["Audio"][1]}, '
                      f'container={components["Container"][1]}, external_auto={external.returncode == 0}', flush=True)
            # Validate JPEG/PCM AVI restoration from ALAC, separately from the archive path.
            if audio:
                encoded = args.output / 'mov/alac' / f'{source.stem}.mov'
                media = args.output / f'{source.stem}.restored-mjpeg.avi'
                cli('restore', encoded, media)
                media.unlink()
    finally:
        (args.output / 'report.json').write_text(json.dumps(records, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(records)} codec/container encodes, archive restores and external checks', flush=True)


if __name__ == '__main__':
    main()
