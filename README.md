# MJXL

**English** | [한국어](README.ko.md)

MJXL is an experimental Windows project that reduces the size of existing MJPEG videos by reversibly recompressing their JPEG frames with JPEG XL. The native tools produce MJXL AVI files that can restore the complete original file byte for byte, with playback through a DirectShow filter.

- Build, conversion, and playback: [Native tools guide (Korean)](native/README.md)
- Validation scope and work log: [STATUS.md (Korean)](STATUS.md)
- The Media Foundation implementation is experimental. Windows Media Player support is incomplete.
- Sample videos, dependencies, build artifacts, and signing certificates are excluded from this repository.

## Native tools at a glance

The native implementation targets VS2026 x64 and uses the experimental AVI FourCC `MJXL`. Each video packet is an independent JXL file containing JPEG reconstruction metadata. PCM audio is copied without modification by default. Add `--flac` to store 8, 16, and 24-bit PCM losslessly as FLAC; 32-bit PCM always stays PCM.

Set `FFMPEG_SDK` to your FFmpeg SDK directory, then run the following commands from the repository root. See the native guide for the required Visual Studio tools, Windows SDK, CMake, and Python environment.

```powershell
.\native\prepare_deps.ps1
.\native\build.ps1

.\dist\Release\mjxl.exe encode .\samples\input.mov .\results\output.avi
.\dist\Release\mjxl.exe verify .\samples\input.mov .\results\output.avi
.\dist\Release\mjxl.exe restore-original .\results\output.avi .\results\restored.mov
.\dist\Release\mjxl.exe restore .\results\output.avi .\results\restored-mjpeg.avi

# Register the DirectShow decoder for the current user.
.\native\register.ps1
```

Provide your own input video. Output files are not overwritten. `restore-original` recreates the original file and checks its SHA-256 hash. `restore` instead builds an MJPEG AVI from the preserved JPEG packets. `verify` compares the rebuilt file with the original, so run it before deleting an original. `encode` fails if it cannot store the data needed for `restore-original`; add `--no-original` to skip it. Files made that way or by earlier versions cannot restore the original file. PotPlayer playback has been verified on the development PC. VLC does not use this DirectShow decoder.

The current native writer supports constant-frame-rate video, up to one audio stream, and classic AVI files smaller than 2 GiB. OpenDML and large-file streaming are not implemented. Windows Media Foundation stalls when reading FLAC inside AVI, so do not use `--flac` for that playback path.

## Python preservation experiments

The remainder of this document describes the Python research tools, which reversibly compress MJPEG packets and reconstruct the entire original MOV file. For the native converter and DirectShow decoder built with VS2026, see the native guide linked above. `MJXL.sln` in the repository root is the VS2026 solution.

## Experimental modes

| Mode | Independent frames | Referenced frames |
| --- | --- | --- |
| `jxl` | Reversible JPEG XL recompression | None |
| `inter` | JXL for the first frame of each GOP | Use the last 32 KiB of the previous JPEG as a DEFLATE dictionary |
| `coeff` | JXL for the first frame of each GOP | Store differences between quantized DCT coefficients at corresponding positions as int16 values, then compress with DEFLATE |
| `hybrid` | JXL for the first frame of each GOP | Select the smallest representation for each frame from JXL, dictionary, and coefficient modes |

The default GOP length is 12. Hybrid selection is greedy per frame, rather than globally optimized. References always use the reconstructed JPEG of the immediately preceding frame. No references cross GOP boundaries.

Coefficient mode targets 8-bit baseline JPEG with a single interleaved scan. DC coefficients are reconstructed as absolute values before computing differences. AC coefficients are compared at corresponding zigzag positions. Motion compensation and entropy-model optimization are not implemented.

To reproduce the original JPEG bit for bit, the tools preserve headers, the original Huffman symbol sequence, final entropy-stream padding bits, and data following the scan. Preserving decoded pixels alone would not provide this guarantee. If the coefficient parser cannot reproduce a JPEG exactly or does not support it, that predictive candidate is discarded in favor of JXL. If JXL itself cannot process the input, the experiment stops.

All regions of the MOV outside the video packets are stored with DEFLATE, including audio, MOV metadata, and padding. Audio compression should therefore be evaluated separately from video compression. Packet positions and bytes are checked against the original file first.

## Running the experiments

The development environment uses Python 3.13, PyAV, NumPy, and libjxl 0.11.2 in the `sgtk` conda environment. The paths below assume a checkout at `W:\proj\mjxl`; adjust them for your installation.

```powershell
$env:PYTHONUTF8 = '1'
$python = "$env:USERPROFILE\miniforge3\envs\sgtk\python.exe"
$jxl = "$env:USERPROFILE\miniforge3\envs\sgtk\Library\bin"
& $python -X utf8 W:\proj\mjxl\mjxl_lab.py --dll-dir $jxl bench W:\proj\mjxl\samples\input.MOV W:\proj\mjxl\results\new-run --gop 12
& $python -X utf8 W:\proj\mjxl\mjxl_lab.py --dll-dir $jxl restore W:\proj\mjxl\results\new-run\input.hybrid.mjxl-lab W:\proj\mjxl\results\restored.MOV
```

Existing output files are never overwritten. Use a new output directory for another run. Source videos are not modified.

## Validation and archive format

Each experiment immediately reverses every JXL frame and every available coefficient candidate and compares the result with the original JPEG. It then writes and rereads each mode's archive and compares the complete reconstructed file with the original. SHA-256 hashes are checked for every record and the whole file.

An experimental archive consists of the eight-byte identifier `MJXLLAB1`, a little-endian uint64 JSON-header length, a UTF-8 JSON header, and concatenated record payloads. The header contains the original file length and hash, plus each record's codec, original and stored lengths, and hash. Each JXL frame is stored as an independent JXL file with JPEG reconstruction metadata. After DEFLATE decompression, a coefficient payload starts with `<IIIIBB`: the byte lengths of the header, symbols, suffix, and coefficient data, followed by the padding-bit count and value. Those sections follow in order. Coefficient differences are little-endian int16 values.

The Python experiments keep the whole file and candidate representations in memory and are not intended for large videos. Inputs are limited to trusted research samples. Random-access indexing, real-time playback, resource limits, and FFmpeg/DirectShow integration are outside the Python archive implementation; the native tools provide a separate playback path.

## Pixel decoding benchmarks

`decode_bench.py` measures MJPEG and JXL decoding to RGB8 pixels. After warmup, it reports the median of three runs, together with per-frame p95 and maximum latency. A new JXL decoder is created for every frame; multithreaded measurements reuse a worker pool. Input is held in memory, so disk access and rendering are excluded. Before timing, each JXL frame is checked for exact reconstruction of the original JPEG.

```powershell
& $python -X utf8 W:\proj\mjxl\decode_bench.py --dll-dir $jxl W:\proj\mjxl\samples\input.MOV - W:\proj\mjxl\results\decode-new.json
& $python -X utf8 W:\proj\mjxl\summarize_results.py W:\proj\mjxl
```

Pass `-` as the archive argument to create JXL frames in memory before benchmarking, or provide an existing archive containing independent JXL frames. The aggregate report is written to `COMPARISON.md`.

## Further work

Extend comparisons with static scenes, camera movement, scene cuts, and low-light noise. Evaluate coefficient compression with header and symbol preservation costs included. Potential experiments include block-motion prediction, frequency-specific coefficient prediction and compression, GOP optimization, and processing-speed improvements. Independent JXL frames remain available when predictive coding increases the stored size.
