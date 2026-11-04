"""실제 샘플과 통제 입력을 사용하는 가역 복원 검사."""
import argparse
from pathlib import Path
import tempfile
import av
import numpy as np
import jpeg_coeff
from mjxl_lab import JXL, restore


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--dll-dir', type=Path, required=True)
    args = parser.parse_args()
    jxl = JXL(args.dll_dir)
    with av.open(str(args.source)) as container:
        packet = next(p for p in container.demux(video=0) if p.size)
        original = bytes(packet)
    independent = jxl.encode(original)
    if jxl.decode(independent, len(original)) != original:
        raise AssertionError('JXL round-trip')
    identical = jpeg_coeff.encode(original, original)
    if jpeg_coeff.decode(identical, original) != original:
        raise AssertionError('Identical frame round-trip')
    state, coefficients = jpeg_coeff.split(original)
    changed = coefficients.copy()
    block, ac = np.argwhere(changed[:, 1:] != 0)[0]
    changed[block, ac + 1] *= -1
    changed_jpeg = jpeg_coeff.join(state, changed)
    residual = jpeg_coeff.encode(changed_jpeg, original)
    if changed_jpeg == original or jpeg_coeff.decode(residual, original) != changed_jpeg:
        raise AssertionError('Changed AC coefficient round-trip')
    if restore(args.archive, jxl) != args.source.read_bytes():
        raise AssertionError('Independent archive round-trip')
    archive = args.archive.read_bytes()
    with tempfile.TemporaryDirectory(prefix='mjxl-validation-') as folder:
        for name, data in [('truncated', archive[:-1]),
                           ('corrupted', archive[:-1] + bytes([archive[-1] ^ 1]))]:
            path = Path(folder) / name
            path.write_bytes(data)
            try:
                restore(path, jxl)
            except (ValueError, RuntimeError) as error:
                print(f'PASS {name}: {type(error).__name__}')
            except Exception as error:
                import zlib
                if not isinstance(error, zlib.error):
                    raise
                print(f'PASS {name}: zlib.error')
            else:
                raise AssertionError(f'Accepted {name} archive')
    print(f'PASS identical frame: JXL={len(independent)}, coefficient P={len(identical)} bytes')
    print(f'PASS changed AC coefficient: coefficient P={len(residual)} bytes')
    print('PASS independent full-file restore')


if __name__ == '__main__':
    main()
