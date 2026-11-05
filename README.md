# MJXL — Motion JPEG XL

**English** | [Korean](README.ko.md)

MJXL is an experimental Windows and Linux project that reduces existing MJPEG videos by reversibly recompressing their JPEG frames with JPEG XL. Its native CLI stores independent JXL frames in AVI or Matroska (MKV) with the experimental `MJXL` FourCC. By default, either container also contains the information needed to restore the complete original file byte for byte. PCM audio is stored as lossless FLAC by default; `--copy-audio` copies it unchanged.

## Why preserve MJPEG this way?

Old camera videos often use Motion JPEG: each video frame is a JPEG image. JPEG XL can reversibly recompress supported JPEG bitstreams and reconstruct the original JPEG bytes exactly. **Motion JPEG XL applies that capability to every frame of an MJPEG video.** It preserves the existing JPEG compression, including its artifacts, without adding another generation of loss.

If the goal is a small viewing copy, a lossy H.265 encode can be a better fit. This project's goal is an archival copy from which the original can be recovered. A lossy transcode may look similar but cannot reproduce the original JPEG bitstreams. Even a lossless encode of decoded pixels does not by itself preserve the original JPEG files or MOV container.

That is why a saving of around 16% can still be useful: it is space recovered while retaining the original, rather than a trade of some fidelity for a much smaller file. Savings depend on the input and are not guaranteed. MJXL combines reversible JPEG-to-JXL recompression, lossless audio storage, and Brotli-compressed container recovery data, so `restore-original` can reproduce the complete source file byte for byte and verify its SHA-256. The original media payloads are not stored a second time.

MJXL is an experimental project name and FourCC, not a standardized video codec. Artifact cleanup would change the image and belongs in a separate derived viewing copy; it is not part of the reversible archive path.

**This is a CMake project on Windows.** Start with the root `CMakeLists.txt`. Visual Studio solutions and projects are generated locally and are not stored in Git. Linux builds the CLI with `native/Makefile`.

## Windows: configure, build, and install

Prerequisites: VS2026 with the v145 C++ toolset and Windows SDK, CMake (the VS-bundled copy is supported), Git, and Python in the `sgtk` conda environment. Set `FFMPEG_SDK` to a Windows FFmpeg SDK containing headers, import libraries, and DLLs. The older `FFMPEG_PATH` variable is also accepted. libjxl 0.11.2 and its dependencies are prepared separately and linked statically with the C++ runtime.

Run from the repository root:

```powershell
.\native\prepare_deps.ps1
# Configure and generate the solution without compiling.
.\native\build.ps1 -ConfigureOnly
# Build all Release targets and copy runtime DLLs and licenses.
.\native\build.ps1
# Install the built decoder and tools for the current user.
.\native\install.cmd
```

Open `_build/windows-vs2026/mjxl.sln` in VS2026 after configuration, or open the root folder as a CMake project. CMake also generates `.slnx` in the build folder. The first full build compiles libjxl and its dependencies; generating the solution is only an early step. Skip `-ConfigureOnly` if you want a full build immediately. The wrapper reports configuration, solution generation, compilation, and runtime copying separately.

The old `_build/vs2026` cache used `native` as its source directory and cannot be reused with the new root entry point. The new default directory keeps it intact. Use `-BuildDirectory _build/windows-local` for another local build tree. Old local root `MJXL.sln` files are not updated or used by the wrapper.

Build outputs are in `dist/Release`. `install.cmd` copies the built files to `%LOCALAPPDATA%\MJXL`, registers the 64-bit DirectShow decoder for the current user, and clears the stale ActiveMovie `Filter Cache64`. It does not build the project. Reopen the player after installation. Use `-DecoderOnly` to install just the decoder or `-Uninstall` to remove the recorded installation. Reinstall after rebuilding to update the local copy. Installation does not change file associations.

## Encode, verify, and restore

Provide your own input video and use a new output path; existing files are not overwritten.

```powershell
New-Item -ItemType Directory -Path results -Force
$mjxl = '.\dist\Release\mjxl.exe'
& $mjxl encode input.mov results\output.avi
# Select the Matroska container by the output extension.
& $mjxl encode input.mov results\output.mkv
& $mjxl verify input.mov results\output.avi
& $mjxl restore-original results\output.avi results\restored.mov
& $mjxl restore results\output.avi results\restored-mjpeg.avi
# Optionally copy the audio unchanged and set JXL effort (default: 7).
& $mjxl encode input.mov results\copied-audio.avi --copy-audio --effort 8
```

`restore-original` reconstructs the complete source file and checks its SHA-256. `restore` instead creates an MJPEG AVI using the preserved JPEG packets and PCM samples. `verify` checks original-file reconstruction against the source, so run it before deleting an original. `encode` fails if it cannot preserve the reconstruction information; `--no-original` explicitly omits it. Such files and AVIs produced by older versions cannot restore the complete original file.

FLAC storage supports 8/16/24-bit PCM; 32-bit PCM remains PCM. Input audio must be PCM, or FLAC with MJXL metadata; other codecs are rejected with or without `--copy-audio`. Original 8-bit PCM is stored in the upper byte of 16-bit FLAC and reconstructed exactly using the AVI's audio metadata. Effort accepts 1–10 and defaults to 7; higher settings are not guaranteed to produce smaller files. Options follow the command. Run `mjxl --help` or `mjxl encode --help` for details.

After successful encoding and verification, a summary shows original bytes, stored bytes, and reduction percentages separately for audio, video, container/recovery data, and the complete file. Audio/video rows count stream payloads; the container/recovery row includes headers, indexes, padding, and original-file recovery information. The total includes all of these costs. Negative reduction means growth; a zero-byte baseline is shown as `n/a`.

`encode` selects the container from `.avi` or `.mkv` (case-insensitive) and rejects other output extensions. MKV uses FFmpeg's Matroska muxer and `V_MS/VFW/FOURCC` with `MJXL`; original-file recovery is stored in the `mjxl-original.bin` attachment with MIME type `application/x-mjxl-original`. FLAC tracks retain the original PCM depth in `MJXL_ORIGINAL_BITS` metadata. `verify`, `restore-original`, and `bench` accept either container; `restore` always writes MJPEG AVI and requires `.avi`. The compressed media payloads are the same for the same input and effort, but container overhead differs. In the four tested samples, MKV was 970–21,638 bytes larger than AVI; smaller overhead is not guaranteed.

MKV encoding, complete original-file restoration, and FFmpeg video/audio decoding passed for four MOV samples in both audio modes. The user confirmed PotPlayer playback and seeking for the 4.2-second PCM and FLAC MKVs. Explorer thumbnails did not appear for those MKVs; the Preview pane was not tested. No thumbnail handler is installed by this change. To repeat the CLI round-trip suite for MKV, add `--container mkv` to `native/validate_cli.py` and use a fresh output directory. The current MKV implementation has been tested on Windows; it has not yet been rebuilt and tested on Linux.

## Playback and compatibility

| Path | Current support |
| --- | --- |
| PotPlayer 64-bit / DirectShow | Playback and seeking verified on development PCs; install the decoder and refresh its filter cache. |
| FFmpeg / ffplay | Requires a build with the `libjxl` decoder and explicit input decoder selection. |
| VLC | Does not use this DirectShow decoder; MJXL playback is not implemented. |
| Media Foundation | Experimental MFT and tests exist; Windows Media Player support is incomplete. |
| Explorer thumbnails / preview | Depends on the installed handler and decoding path; the DirectShow installer alone does not establish support. |

The user's earlier PotPlayer codec-selection problem was resolved by clearing the filter cache. `install.cmd` includes that step. Media Foundation registration and MSIX packaging are separate experiments, not part of this installer. Windows Media Foundation stalls on FLAC inside AVI, and Windows has no built-in DirectShow decoder for it; encode with `--copy-audio` for those paths.

## Linux CLI and FFmpeg

The CLI uses a C++20 compiler and system FFmpeg, libjxl, and Brotli libraries. Ubuntu 24.04 WSL2 x86-64 with GCC 13 was verified. DirectShow and MFT are Windows-only components.

```bash
sudo apt install build-essential pkg-config libjxl-dev libbrotli-dev libavformat-dev libavcodec-dev libavutil-dev ffmpeg
make -C native -j4
mkdir -p results
./dist/linux/mjxl encode input.mov results/output.avi
./dist/linux/mjxl verify input.mov results/output.avi
./dist/linux/mjxl restore-original results/output.avi results/restored.mov
sha256sum input.mov results/restored.mov
ffplay -vcodec libjxl results/output.avi
ffmpeg -v error -xerror -c:v libjxl -i results/output.avi -map 0:v:0 -map '0:a:0?' -f null -
```

Build on the target distribution: the binary dynamically links system libraries, and compression sizes can vary by libjxl version. Check `ffmpeg -decoders` for `libjxl`. Put `-c:v libjxl` before FFmpeg's `-i`; ffplay uses `-vcodec libjxl`. These commands decode pixels, not the original JPEG file bytes. No FFmpeg FourCC mapping or GStreamer plugin is added.

## Planned playback support

Future work includes GStreamer (`gst`) integration and playback on macOS. Neither is implemented or validated yet. These plans aim to make the same reversible archives usable beyond the current Windows DirectShow and Linux FFmpeg paths; they are not a claim that existing macOS players can open MJXL today.

## Scope and validation

The native writer supports constant frame rate, one video stream, and at most one audio stream. The custom AVI writer is limited to classic AVI smaller than 2 GiB; MKV uses FFmpeg. Both paths hold input packets in memory, and large MKV files have not been validated. OpenDML, large-file streaming, artifact correction, and motion prediction are not implemented.

Existing records cover seven samples / 1,332 frames, exact JPEG and PCM restoration, original-file SHA-256 checks, and DirectShow playback and seeking. Linux PCM and FLAC encodes passed original-file round trips and FFmpeg decoding. Linux ffplay was checked with SDL dummy outputs; visible playback and listening were not verified. Historical results are not a guarantee for every file or PC.

- [Native tools and detailed commands](native/README.md)
- [Validation records and prior work log](STATUS.md)
- [Current work log and PCM playback noise investigation](doc/WORKLOG.md)
- Python experiments: [English](doc/experiments.md) / [Korean](doc/experiments.ko.md)
- Measurements: [COMPARISON.md](COMPARISON.md), [RESULTS.md](RESULTS.md)

Sources and build definitions belong in Git. Samples, results, `_deps`, `_build`, `dist`, generated `.sln`/`.slnx`/`.vcxproj` files, and signing certificates do not.

Source comments, script messages, and default documents are English. Korean document companions use `.ko.md`. Exact stored PCM does not guarantee artifact-free player output: PotPlayer clicked periodically on PCM AVIs written with an 18-byte `strf`. The writer now uses a 16-byte `strf`; AVIs encoded earlier keep the old header until they are re-encoded.
