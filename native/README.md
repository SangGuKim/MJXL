# Native MJXL encoder and DirectShow decoder

**English** | [Korean](README.ko.md)

This experimental codec is built with VS2026 x64. Each video frame is reversibly recompressed to an independent JPEG XL file and stored in AVI under the experimental `MJXL` FourCC. PCM audio is stored as lossless FLAC by default; `--copy-audio` copies it unchanged.

## CMake configuration, build, and solutions

The Windows entry point is the repository-root `CMakeLists.txt`. A fresh source checkout does not contain Visual Studio solutions or projects. Run from the repository root:

```powershell
# Set FFMPEG_SDK to your SDK; FFMPEG_PATH remains a compatible alternative.
$env:FFMPEG_SDK
.\native\prepare_deps.ps1
# Configure and generate projects only, without compiling.
.\native\build.ps1 -ConfigureOnly
# Build all Release targets and copy runtime DLLs and licenses.
.\native\build.ps1
```

Open `_build/windows-vs2026/mjxl.sln` in VS2026, or open the root folder as a CMake project. CMake also generates `.slnx` in the build folder. Solutions, projects, and caches are excluded from Git. An old local root `MJXL.sln` is neither used nor updated by the new wrapper.

The old `_build/vs2026` cache used `native` as its source directory. The new default build folder preserves that cache without reusing it. Use `-BuildDirectory _build/windows-local` for another local tree or PC. Caches belonging to another source directory are rejected with an explanation and are not deleted. Omit the `-ConfigureOnly` step to build everything immediately.

Required tools are VS2026 with v145 and Windows SDK, CMake (including the VS-bundled copy), Git, and Python from the `sgtk` conda environment. `FFMPEG_SDK` takes precedence over the older `FFMPEG_PATH`; it must provide FFmpeg headers, import libraries, and DLLs. Python is used to generate the legacy solution, not to run the encoder or decoder.

libjxl 0.11.2 and its required dependencies are built statically from `_deps/libjxl`, together with the C++ runtime. This avoids conflicts with an old MSVCP140.dll already loaded by a host. Microsoft DirectShow base-class sources are preserved; two VS2026 compatibility fixes are applied only to a build-folder copy.

## Linux build

The standalone CLI uses GCC or Clang and system FFmpeg, libjxl, and Brotli. DirectShow and Media Foundation components are Windows-only. The executable is written to `dist/linux/mjxl`.

```bash
sudo apt install build-essential pkg-config libjxl-dev libbrotli-dev libavformat-dev libavcodec-dev libavutil-dev
make -C native
mkdir -p results
./dist/linux/mjxl encode input.mov results/output.avi
./dist/linux/mjxl verify input.mov results/output.avi
./dist/linux/mjxl restore-original results/output.avi results/restored.mov
sha256sum input.mov results/restored.mov
# Supply your own MOV samples and a new output directory.
python3 native/validate_cli.py --binary dist/linux/mjxl --samples samples --output results/linux-check
```

Full linking and execution were verified on Ubuntu 24.04 WSL2 x86-64 with GCC 13. The binary dynamically links system libraries and is not a universal Linux executable. Compression sizes can vary with libjxl versions. A C++20 compiler and standard library with `std::format` support are required. No Linux player plugin is installed.

## Encoding, verification, and restoration

`encode` selects AVI or MKV by the output extension (`.avi` / `.mkv`, case-insensitive); other extensions are rejected. For example, `mjxl encode input.mov output.mkv` uses FFmpeg's Matroska muxer with `V_MS/VFW/FOURCC` and FourCC `MJXL`. `verify`, `restore-original`, and `bench` read both containers. `restore` still produces MJPEG AVI and requires `.avi`.

MKV stores Brotli-compressed original-file recovery data as the `mjxl-original.bin` attachment (`application/x-mjxl-original`) instead of an AVI `mjxo` chunk. Original PCM depth is stored in the FLAC track's `MJXL_ORIGINAL_BITS` tag instead of `mjxa`. The CLI recognizes this recovery attachment but rejects unrelated extra streams. `--no-original` also works with MKV. Container overhead can be larger than AVI: the four tested samples grew by 970–21,638 bytes with identical media payloads and effort. Neither the AVI DirectShow graph test nor the MFT test establishes MKV support.

On Windows, all four MOV samples passed MKV PCM/FLAC encoding, exact original-file reconstruction, and FFmpeg video/audio decoding. Synthetic video-only and 16/24/32-bit stereo cases also passed, including PCM32 fallback. PotPlayer playback and seeking were confirmed by the user on the 4.2-second PCM and FLAC MKVs. Explorer thumbnails were absent; the Preview pane and Linux MKV build were not tested. No shell thumbnail handler is installed. Run `native/validate_cli.py --container mkv` with the usual binary/sample/output arguments and a fresh output folder to repeat the MKV round-trip suite.

```powershell
$mjxl = '.\dist\Release\mjxl.exe'
& $mjxl encode input.mov results\example.avi
& $mjxl verify input.mov results\example.avi
& $mjxl restore results\example.avi results\restored-mjpeg.avi
& $mjxl restore-original results\example.avi results\restored.mov
& $mjxl bench results\example.avi
```

`encode` checks exact JPEG reconstruction and, when applicable, FLAC-to-PCM reconstruction before writing. It rereads the output container and compares every JPEG packet and all PCM bytes. Run `mjxl --help` or `mjxl encode --help` for command-specific options. Options may appear anywhere after the command, but not before it. Unsupported options are rejected.

`-e 1-10` / `--effort 1-10` selects JXL effort; the default is 7. Higher effort does not guarantee a smaller result. One recorded 21-frame experiment found effort 8 roughly three times slower for about 0.2 percentage points more savings; 9 and 10 were over 30 times slower without further savings in that experiment. Other measurements are recorded in [STATUS.md](../STATUS.md). Existing files are not overwritten. Output uses `.partial` until verification succeeds.

`encode` stores original non-packet bytes and packet placement in a trailing `mjxo` chunk, compressed with Brotli. Recorded overhead is approximately 1.4-6.2 KiB per sample. Before finalizing the output name, the tool verifies that the AVI alone reconstructs the complete original file. `restore-original` rebuilds that file and checks SHA-256. `verify` also compares complete reconstruction with the source; run it before deleting an original.

If packet positions or bytes prevent exact reconstruction, encoding fails. `--no-original` explicitly omits recovery information. Files made that way, and older AVIs without `mjxo`, cannot restore the whole original; `verify` rejects them unless `--no-original` is given. `restore` instead rebuilds an MJPEG AVI from exact JPEG packets and PCM bytes without recreating the original container.

### FLAC audio

FLAC storage is the default; `--copy-audio` keeps the source PCM instead. It uses the FFmpeg FLAC encoder. In the recorded samples, audio shrank about 34%, but total AVI savings improved by only about 0.365%. The FFmpeg encoder accepts s16/s32 input, so original 8-bit PCM is stored in the upper byte of 16-bit FLAC. Original PCM depth is recorded in an `mjxa` chunk inside `hdrl`. Original 24-bit PCM remains 24-bit FLAC; 32-bit PCM remains PCM.

Both restore commands use this metadata to recover exact original PCM bytes. FLAC AVI without `mjxa` is rejected. Audio equality is a data-preservation check, not a guarantee of artifact-free playback through every player. PCM AVI uses a 16-byte `strf` because PotPlayer clicked periodically on the 18-byte form; see [the work log](../doc/WORKLOG.md).

## Installation on another PC

After building, copy the decoder and tools to a permanent local folder and register them for the current user. Administrator privileges are not required. The default destination is `%LOCALAPPDATA%\MJXL`; paths are resolved relative to the installer, so the checkout drive may vary.

```bat
native\install.cmd
native\install.cmd -DecoderOnly
native\install.cmd -Destination C:\Tools\MJXL
native\install.cmd -Uninstall
```

The installer copies the decoder, CLI, required FFmpeg DLLs, and licenses, registers the decoder, and clears the DirectShow filter cache. It does not build. `-DecoderOnly` installs only playback support; `-Destination` selects another folder. Uninstall removes only manifest-recorded files. Playback requires a 64-bit player. Reinstall after rebuilding to update the local copy.

`register.ps1` registers the build-folder DLL for development; removing that drive then breaks playback. Neither script changes other codec priorities or file associations.

## PotPlayer and DirectShow

```powershell
# Development registration; omit -AllUsers for current-user registration.
.\native\register.ps1 -AllUsers
.\dist\Release\mjxl_graph_test.exe .\dist\Release\mjxl_decoder.ax results\example.avi
```

`-AllUsers` registers for all users and requests administrator elevation when necessary. Use `-Unregister` to unregister with the same scope. Without `-AllUsers`, registration is per user and does not require administrator privileges. Per-user PotPlayer playback was verified after rebuilding the filter cache. The earlier automatic codec-selection failure was resolved by clearing the stale ActiveMovie `Filter Cache64`; `install.cmd` already does this. Reopen the player after installation or registration.

The decoder converts JXL directly to RGB24 instead of reconstructing JPEG and decoding it again. Independent frames allow seeking without reconstructing preceding frames. The default graph test uses Windows AVI Splitter and Null Renderer. Add `--registered` to test COM creation without preloading the DLL. Add `--auto` to test automatic DirectShow selection and real rendering; this may open an ActiveMovie window and produce audio.

Windows DirectShow has no built-in decoder for FLAC in AVI, so automatic graph playback may leave that audio disconnected; PotPlayer uses its own audio decoder. VLC does not use the DirectShow filter. Explorer thumbnails and preview depend on the installed handler and decoding path.

## FFmpeg and ffplay

The tested SDK includes `libjxl`, but no automatic mapping for the experimental `MJXL` FourCC. Explicitly select the input decoder:

```powershell
$ffmpegRoot = if ($env:FFMPEG_SDK) { $env:FFMPEG_SDK } else { $env:FFMPEG_PATH }
& "$ffmpegRoot\bin\ffmpeg.exe" -c:v libjxl -i results\example.avi -map 0:v:0 -map '0:a:0?' -f null -
```

Linux FFmpeg with `libjxl` also reads these files without a custom plugin. Ubuntu 24.04 FFmpeg 6.1.1 was tested; check other builds with `ffmpeg -decoders`.

```bash
sudo apt install ffmpeg
ffplay -vcodec libjxl output.avi
ffmpeg -v error -xerror -c:v libjxl -i output.avi -map 0:v:0 -map '0:a:0?' -f null -
```

Use `-vcodec libjxl` for ffplay and place FFmpeg `-c:v libjxl` before `-i`. Optional audio mapping also supports silent files. Fourteen PCM/FLAC AVIs passed full video/audio decoding. ffplay reached the end of short PCM and FLAC files with SDL dummy outputs; visible Linux playback and listening were not verified. These commands decode pixels; exact original-file recovery uses `mjxl restore-original`. No FFmpeg FourCC mapping or GStreamer plugin was added.

## Current limits and validation

- Constant frame rate, one video stream, and at most one audio stream.
- Input PCM: unsigned 8-bit or signed little-endian 16/24/32-bit. Real samples cover PCM8 mono; synthetic tests cover additional formats.
- Classic AVI below 2 GiB; no OpenDML or large-file streaming.
- Nonzero stream start offsets, variable frame rate, and extra streams are rejected rather than silently discarded.
- Input packets are held in memory.
- No artifact correction or inter-frame prediction.
- Close programs using the decoder before rebuilding it; locked files are not forcibly replaced.

`native/validate_native.py` verifies native Windows tools with FLAC storage; pass `--copy-audio` for copied PCM. Every `restore-original` result is compared with the source by SHA-256. Samples and outputs in `samples/`, `results/native-original/`, and `results/native-original-flac/` are excluded from Git. Use `validate_cli.py` for standalone cross-platform CLI testing.

## Experimental Media Foundation support

`mjxl_mft.dll` and `mjxl_mft_test.exe` implement an experimental MFT. Local MFT tests covered 1,332 frames from seven AVIs, RGB32 output, seeking, timestamps, and error handling. Run `mjxl_mft_test.exe <MJXL AVI> --local <mjxl_mft.dll>` without global registration.

`register_mft.ps1` copies the MFT to Program Files and registers it for desktop use after administrator elevation. `-Unregister` removes registration. That registration alone did not enable Windows Media Player playback; support remains incomplete and DirectShow is the recommended current path. Media Foundation AVI sources stall on FLAC audio, so encode with `--copy-audio` for MFT experiments.

`package_mft.ps1` creates a media-extension MSIX using Windows SDK. `-Sign` creates or reuses a 30-day development code-signing certificate in the current-user My store. It does not add trust-store entries or install the package. The development package was not installed and validated in Media Player and is not a production installer. Certificates, private keys, and signed packages are excluded from Git.
