# First-sample experimental results

**English** | [Korean](RESULTS.ko.md)

## Input and settings

- Input: `samples/IMG_20040318_225819.MOV`
- Original size: 1,301,421 bytes
- Original SHA-256: `0d015a314f1ce475984a5296b79621065a2be0963c4d4b744dd13c25fb5aeeb1`
- Video: MJPEG, 640 x 480, 28 frames, 15 fps, approximately 1.87 seconds
- Audio: unsigned 8-bit PCM, 8,040 Hz
- libjxl: 0.11.2, reversible JPEG recompression, default encoder settings
- GOP: 12 frames
- All sizes include the experimental JSON header, audio, and original-container preservation data.

## Full-file comparison

| Mode | Stored bytes | Savings over original | P frames | Full-file restoration |
| --- | ---: | ---: | ---: | --- |
| Independent JXL | 1,128,261 | 13.305% | 0 | Exact |
| JXL + JPEG-dictionary P | 1,279,085 | 1.716% | 25 | Exact |
| JXL + DCT-coefficient P | 3,081,875 | -136.808% | 25 | Exact |
| Smallest representation per frame | 1,128,261 | 13.305% | 0 | Exact |

Every mode passed record-level and full-MOV byte comparison and SHA-256 verification. The source was not modified. Frame measurements are recorded in `results/coeff-v1/IMG_20040318_225819.report.json`.

Coefficient mode is an initial baseline that serializes same-position coefficient differences as int16 values and preserves the original Huffman symbols. It has no motion compensation, frequency-specific entropy model, or symbol prediction. These results do not rule out efficient reversible inter-frame compression. Independent JXL was smallest among the implemented candidates for this sample.

## Controlled-input validation

The first JPEG was used as both reference and current frame. These are controlled inputs, not measurements of a real static scene.

| Input | JXL payload | Coefficient P payload |
| --- | ---: | ---: |
| Identical first frame | 47,657 bytes | 40,942 bytes |
| One AC coefficient with its sign reversed | Not measured | 40,948 bytes |

Both P inputs reconstructed the exact JPEG bytes. This validates the reference/residual path without establishing efficiency on ordinary video. Preserving headers and symbols still has considerable cost even for identical frames.

A separate process restored the hybrid archive to MOV and passed byte comparison. Both a truncated archive and a corrupted payload were rejected.

## Further experiments

Compare actual static-background footage using the same implementation and GOP. Candidate improvements include reference compression of symbols, frequency-specific coefficient layout/models, and motion compensation. These are research archives, not playable FFmpeg or PotPlayer containers; native MJXL AVI is a separate implementation.
