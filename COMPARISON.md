# Sample comparison

**English** | [Korean](COMPARISON.ko.md)

Stored sizes include original audio, container preservation data, and experimental archive headers. Negative savings mean a larger file.

| Video | Frames | Original bytes | JXL savings | Coefficient P size / original | Hybrid P frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| IMG_20040318_225819.MOV | 28 | 1,301,421 | 13.31% | 2.37x | 0 |
| IMG_20040320_230212.MOV | 167 | 7,889,241 | 12.87% | 2.52x | 0 |
| IMG_20040321_000945.MOV | 79 | 3,537,817 | 12.84% | 2.69x | 0 |
| IMG_20040322_025026.MOV | 181 | 8,014,661 | 12.83% | 2.29x | 0 |
| IMG_20040325_190603.MOV | 132 | 6,355,373 | 12.33% | 2.13x | 0 |
| IMG_20040326_055048.MOV | 160 | 6,529,405 | 19.74% | 2.46x | 0 |
| IMG_20060430_203434.MOV | 585 | 27,003,974 | 12.70% | Unsupported (JXL fallback) | 0 |

## Totals

Verified 7 source files totaling 60,631,892 bytes.

| Mode | Stored bytes | Savings |
| --- | ---: | ---: |
| jxl | 52,457,450 | 13.48% |
| inter | 59,655,716 | 1.61% |
| coeff | 103,953,662 | -71.45% |
| hybrid | 52,457,450 | 13.48% |

All results passed full-file byte comparison and SHA-256 verification. Coefficient P is an initial baseline without motion compensation. Its totals include JXL fallback for unsupported inputs and do not measure pure P-frame performance or the limits of reversible inter-frame compression.

## Pixel-decoding throughput

Inputs were decoded from memory to RGB8 after warmup. Throughput is the median of three runs. Disk access, audio, rendering, and restoration filters are excluded. JXL workers count libjxl workers, not all process threads. Concurrent measurements ran alongside compression; isolated measurements ran after compression completed.

| Source | Conditions | Source fps | Decoder | Workers | Decode fps | p95 ms | Maximum ms |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |
| IMG_20060430_203434.MOV | Concurrent compression | 30 | mjpeg | Single | 803.4 | 1.63 | 3.13 |
| IMG_20060430_203434.MOV | Concurrent compression | 30 | jxl | Single | 169.3 | 8.72 | 11.60 |
| IMG_20060430_203434.MOV | Concurrent compression | 30 | jxl | 4 | 296.0 | 4.35 | 5.55 |
| IMG_20060430_203434.MOV | After compression | 30 | mjpeg | Single | 912.6 | 1.29 | 1.69 |
| IMG_20060430_203434.MOV | After compression | 30 | jxl | Single | 208.2 | 6.00 | 13.91 |
| IMG_20060430_203434.MOV | After compression | 30 | jxl | 4 | 403.2 | 3.19 | 4.56 |
| IMG_20040318_225819.MOV | Concurrent compression | 15 | mjpeg | Single | 825.6 | Not measured | Not measured |
| IMG_20040318_225819.MOV | Concurrent compression | 15 | jxl | Single | 165.2 | Not measured | Not measured |
| IMG_20040318_225819.MOV | Concurrent compression | 15 | jxl | 4 | 225.9 | Not measured | Not measured |
