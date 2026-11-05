# MJXL

**English** | [Korean](README.ko.md)

MJXL is an experimental Windows and Linux project that reduces existing MJPEG videos by reversibly recompressing their JPEG frames with JPEG XL. Its native CLI stores independent JXL frames in AVI with the experimental `MJXL` FourCC. By default, the AVI also contains the information needed to restore the complete original file byte for byte. PCM audio is stored as lossless FLAC by default; `--copy-audio` copies it unchanged.

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
& $mjxl verify input.mov results\output.avi
& $mjxl restore-original results\output.avi results\restored.mov
& $mjxl restore results\output.avi results\restored-mjpeg.avi
# Optionally copy the audio unchanged and set JXL effort (default: 7).
& $mjxl encode input.mov results\copied-audio.avi --copy-audio --effort 8
```

`restore-original` reconstructs the complete source file and checks its SHA-256. `restore` instead creates an MJPEG AVI using the preserved JPEG packets and PCM samples. `verify` checks original-file reconstruction against the source, so run it before deleting an original. `encode` fails if it cannot preserve the reconstruction information; `--no-original` explicitly omits it. Such files and AVIs produced by older versions cannot restore the complete original file.

FLAC storage supports 8/16/24-bit PCM; 32-bit PCM remains PCM. Input audio must be PCM, or FLAC with MJXL metadata; other codecs are rejected with or without `--copy-audio`. Original 8-bit PCM is stored in the upper byte of 16-bit FLAC and reconstructed exactly using the AVI's audio metadata. Effort accepts 1–10 and defaults to 7; higher settings are not guaranteed to produce smaller files. Options follow the command. Run `mjxl --help` or `mjxl encode --help` for details.

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

## Scope and validation

The native writer supports constant frame rate, one video stream, at most one audio stream, and classic AVI smaller than 2 GiB. It holds input packets in memory. OpenDML, large-file streaming, artifact correction, and motion prediction are not implemented.

Existing records cover seven samples / 1,332 frames, exact JPEG and PCM restoration, original-file SHA-256 checks, and DirectShow playback and seeking. Linux PCM and FLAC encodes passed original-file round trips and FFmpeg decoding. Linux ffplay was checked with SDL dummy outputs; visible playback and listening were not verified. Historical results are not a guarantee for every file or PC.

- [Native tools and detailed commands](native/README.md)
- [Validation records and prior work log](STATUS.md)
- [Current work log and PCM playback noise investigation](doc/WORKLOG.md)
- Python experiments: [English](doc/experiments.md) / [Korean](doc/experiments.ko.md)
- Measurements: [COMPARISON.md](COMPARISON.md), [RESULTS.md](RESULTS.md)

Sources and build definitions belong in Git. Samples, results, `_deps`, `_build`, `dist`, generated `.sln`/`.slnx`/`.vcxproj` files, and signing certificates do not.

Source comments, script messages, and default documents are English. Korean document companions use `.ko.md`. Exact stored PCM does not guarantee artifact-free player output: PotPlayer clicked periodically on PCM AVIs written with an 18-byte `strf`. The writer now uses a 16-byte `strf`; AVIs encoded earlier keep the old header until they are re-encoded.
