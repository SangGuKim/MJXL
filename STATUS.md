# Project status and validation history

**English** | [Korean](STATUS.ko.md)

This record was assembled on 2026-11-05 from existing documents, sources, build artifacts, and reports. Unless an entry explicitly says otherwise, its numbers describe earlier tests, not tests rerun while translating this document. No separate work log existed at the initial review; current work is recorded in [doc/WORKLOG.md](doc/WORKLOG.md).

## Current notes

- The user confirmed that clearing the stale DirectShow filter cache resolved the earlier PotPlayer codec-selection problem. `native/install.cmd` now performs local installation, per-user registration, and cache clearing.
- Windows builds now start at root `CMakeLists.txt`; generated solutions/projects are local artifacts excluded from Git. See the root README for the new build directory and `-ConfigureOnly`.
- Periodic clicking in PotPlayer during PCM AVI playback of `IMG_20040402_034913` was traced to the 18-byte audio `strf` written by the native AVI writer. The writer now uses 16 bytes, and the user confirmed a newly encoded AVI sounds clean. AVIs encoded earlier keep the old header until re-encoded. Details are in the current work log.
- FLAC is now the default audio storage at the user's request. `--copy-audio` replaces `--flac` and copies the source audio unchanged. Entries below that mention PCM as the default or the `--flac` option describe the earlier behavior.
- English is the default language for source comments, script messages, and documents. Korean document copies use `.ko.md`.

## Implemented functionality

- Python research tools preserve JPEG packets and the remaining MOV regions to reconstruct the complete original MOV byte for byte.
- Native tools use libjxl reversible JPEG recompression to store independent JXL frames in `MJXL` AVI. PCM is copied by default; `--flac` losslessly compresses 8/16/24-bit PCM. Original 32-bit PCM always remains PCM.
- Native `restore-original` reconstructs the entire original file byte for byte. `restore` creates an MJPEG AVI from the preserved JPEG/PCM data without recreating the original container.
- The DirectShow filter decodes JXL directly to RGB24. The CLI, filter, and graph-test executables were present in `dist/Release` during the original review.

## Initial validation records

- Research archives: seven originals totaling 60,631,892 bytes became 52,457,450 bytes, saving 13.48%. Full-file byte and SHA-256 comparisons passed.
- Dictionary and DCT-coefficient inter-frame prediction were worse than independent JXL on these samples. Hybrid mode selected no P frames.
- Native reports: seven files / 1,332 frames passed exact JPEG and 556,392-byte PCM comparisons, DirectShow playback, and midpoint seeking.
- Initial native AVI total: 52,374,566 bytes, approximately 13.62% smaller than the originals.
- Earlier RGB decoding measurements reached approximately 403 fps with four JXL workers on the 30 fps sample. Inputs were in memory; rendering was excluded.

## Initial follow-up items

- At the initial review, the build had not been reproduced on that PC. An old user-specific JXL_ROOT default remained in CMake; later entries record its removal.
- Some README/RESULTS text still described research archives without playback support; distinguish that format from native AVI.
- Inputs are held in memory and output is classic AVI below 2 GiB. OpenDML and large-file streaming are not implemented.
- Expand testing beyond PCM8 mono to other audio formats, timestamps, and malformed inputs. JPEG/PCM byte comparison alone does not prove timing or displayed-color equality.
- Per-frame `jbrd` boxes account for approximately 1.17% of file size and are mostly repetitive. Sharing tables was estimated to save another roughly 1.1%, but was deliberately not implemented to preserve independent JXL frames.

## Repository

The Git repository was initialized during the earlier work. Samples, results, dependencies, build outputs, and video extensions are excluded. Do not add source or converted videos. Generated Visual Studio files are now excluded as described above.

## Build and playback on another PC: 2026-11-05

- Completed x64 Release with VS2026 18.11, MSVC 19.51, Windows SDK 10.0.26100, and VS-bundled CMake 4.3.1. Used `_build/vs2026-cymon`, separate from the old cache.
- Used FFmpeg 9.0.2 SDK via `FFMPEG_SDK=c:\sdk\ffmpeg`. Build scripts now prefer FFMPEG_SDK while accepting FFMPEG_PATH. Removed the old user-specific JXL SDK path.
- Registered `dist/Release/mjxl_decoder.ax` for the current user. Dependency inspection showed only three Windows system DLLs; installing the decoder itself does not require FFmpeg or Python.
- All seven existing AVIs passed JPEG/PCM byte comparisons, DirectShow playback, and midpoint seeking with the new binaries.
- Newly encoded and restored the first sample, verifying 28 JPEG frames and 15,008 PCM bytes. Outputs were kept under ignored `results/native-current-pc`.
- Added `--registered` and `--auto` graph tests. COM registration and automatic DirectShow selection passed playback/seeking. The automatic test displayed an ActiveMovie window, and the user confirmed playback.
- PotPlayer internal file opening worked, and playback information identified MJXL JPEG XL Decoder.
- The user reported that Explorer double-click opened the Windows default player, while sending a file to PotPlayer produced a missing-MJXL error. Legacy association registry entries alone do not prove the actual default application.
- A fresh PotPlayer instance then played through internal file opening, including 15 fps playlist items and a 30 fps file. At that point the send-to failure remained unexplained; later cache experiments and user confirmation supersede that uncertainty.
- Windows default-player and Explorer thumbnail/preview support had not been established. Treat them separately from DirectShow acceptance. Packaging, other media paths, and 32-bit support remained follow-up work.

## Per-user registration comparison: 2026-11-05

At the user request, removed all-user MJXL COM/filter registrations and registered only for the current user. Earlier registrations and ActiveMovie caches were backed up in `results/user-scope-*`. Playback tests began with the filter-cache key absent.

Confirmed no system-wide 32-bit or 64-bit MJXL COM registration. Per-user registered creation and automatic selection without DLL preloading each passed 28-frame playback and 15-frame seeking. The recreated Filter Cache64 contained MJXL. A fresh PotPlayer displayed the 30 fps sample and loaded `mjxl_decoder.ax`. All-user registration was therefore not required on that PC. Thumbnails visible during that test may have come from an existing cache and did not prove new thumbnail generation. Do not generalize to other accounts or PCs.

## Media Foundation implementation on hold: 2026-11-05

Implemented an MJXL MFT, test tool, desktop registration script, and Media Player MSIX sources. Local MFT tests passed 1,332 frames from seven AVIs, seeking, timestamps, and error handling. A globally registered desktop path passed 585 frames. Media Player still reported unsupported MJXL; actual Media Player playback was not confirmed.

Created a development package with WinRT activation and a `windows.mediaCodec` extension, signed with a 30-day self-signed certificate. The user deferred installation, so neither trust-store registration nor MSIX installation was performed. The certificate is in the current-user My store, not a trusted store. The desktop MFT remained installed. DirectShow remains the current playback path; retain MFT/package sources for investigation. Package creation or Source Reader success does not establish Media Player support.

## MFT review and fixes: 2026-11-05

An MFT loaded without global registration passed desktop Media Engine playback, end-of-stream seeking, and replay without errors; the displayed orientation was correct. Media Player failure may therefore involve activation/deployment rather than basic Media Foundation compatibility, but this was not confirmed because the MSIX was not installed.

Reproduced and fixed two defects: input was rejected after completed draining unless flushed, and RGB32 output without an explicit stride did not follow the Media Foundation bottom-up default. Negative strides are now supported, and the current output type reports its actual stride.

Added three stride cases and drain/resume tests to `mjxl_mft_test`. Local MFT tests passed all seven AVIs / 1,332 frames. Global registration was not retested at that point. Reran `register_mft.ps1`, registering the fixed binary at `C:\Program Files\MJXL\MFT\2DC5A95EDC7AF1AE`; its hash matched the current build. The older `292459DB87F6D2CF` directory remained but was no longer registered.

## Optional FLAC audio storage: 2026-11-05

Added FLAC to `mjxl encode`. Because total savings were small, the user requested PCM as the default and `--flac` as opt-in. FFmpeg FLAC accepts s16/s32, so original PCM8 is stored in the upper byte of 16-bit FLAC. Original depth is recorded in an `mjxa` chunk inside `hdrl`; FLAC AVI without it is rejected during restoration. libFLAC could store 8-bit directly, but adding that dependency was deferred. Original PCM32 remains PCM.

Across seven samples, PCM AVI totaled 52,374,566 bytes and FLAC AVI 52,183,508 bytes, a reduction of 191,058 bytes (0.365%). Savings against the originals improved from 13.62% to 13.93%. Block sizes are 2,048 samples at rates up to 16 kHz and 4,096 above that, instead of FFmpeg defaults.

Tests passed exact JPEG reconstruction for 1,332 frames, 556,392 PCM bytes, and DirectShow playback/seeking. Synthetic 16/24-bit audio with 1-8 channels and 8-bit stereo passed restoration and independent FFmpeg decoding. Confirmed PCM32 passthrough, verify/restore of existing PCM AVI, and rejection of corrupt FLAC frames or missing audio metadata.

Windows AVI Splitter calculated incorrect FLAC stream lengths and failed seeking when nBlockAlign did not describe samples per frame. Recording samples per FLAC frame fixed that behavior. Windows DirectShow has no built-in decoder for this FLAC stream, so its audio is not connected by automatic Windows graph playback.

PotPlayer displayed FLAC AVI, loaded the decoder, and produced audio-session signals. Measurements were made while system audio was muted, so audible quality and A/V sync were not confirmed. FLAC and PCM session peaks tracked over time, with FLAC approximately 0.75% higher. File samples all matched `(u8-128)*256`, so the discrepancy was not file scaling. An earlier large peak difference came from accidentally measuring the next playlist video.

New-file Explorer thumbnails were generated. However, Media Foundation AVI sources stalled when reading FLAC: both Source Reader audio reads and Media Engine playback stalled, also with FFmpeg-created FLAC AVI. The same streams in MKV passed Media Engine playback and thumbnail creation. Use PCM AVI or MKV for that path; native MKV output was not implemented.

## Complete original-file recovery: 2026-11-05

`mjxl encode` now stores the original bytes outside audio/video packets, packet placement, and SHA-256 in a trailing `mjxo` chunk. The non-packet bytes and layout are compressed with Brotli already available through libjxl. `restore-original` rebuilds the source from those data and the preserved packets, then checks its hash.

All seven samples restored with matching SHA-256 from both PCM and FLAC storage. Total added cost was 35,882 bytes. PCM AVI totaled 52,410,448 bytes (13.56% savings); FLAC totaled 52,219,390 bytes (13.87%), both smaller than the research archive at 13.48% savings.

Verified exact bytes for front-moov files, additional metadata, fragmented MOV, silent MOV, trailing file bytes, AVI inputs, and 16/24/32-bit PCM. Rejected corrupt chunk version/size/hash/tables/remainder bytes, excessive sizes or packet counts, truncated input, and corrupt video frames without leaving output files.

AVI containing `mjxo` passed FFmpeg, automatic DirectShow graphs, Media Foundation tests, Media Engine playback, PotPlayer, and fresh thumbnail generation. Encoding fails if recovery information cannot be created; only `--no-original` omits it. Verify rejects files without that information unless `--no-original` is supplied. Earlier AVIs lack the chunk and cannot recover the original container. Verify a newly encoded file before deleting its source.

A divide-by-zero defect for silent input, introduced with FLAC support in commit `8e6c14f`, was found and fixed.

## JPEG XL effort selection: 2026-11-05

Added `-e` / `--effort 1-10` to encode, defaulting to the prior setting 7 with identical output. Options can appear anywhere after the command but not before it; options unsupported by a command are rejected. Encode reports its savings percentage.

For the 28-frame sample, efforts 1, 3, 5, 7, 8, 9, and 10 all restored the exact source. Sizes at 7/8/9/10 were 1,126,022 / 1,124,182 / 1,122,746 / 1,124,410 bytes, respectively, with times of 0.6 / 1.1 / 9.6 / 11.3 seconds. These are measurements for that run, not universal monotonic size guarantees.

## CLI cleanup and initial Linux compilation: 2026-11-05

Moved command/option definitions into tables in `cli.cpp` shared by parsing and help. No argument-parsing dependency was added. Internal text is UTF-8; Windows-specific argument handling is confined to `wmain`. Windows encode and exact restoration passed with Korean paths.

`native/Makefile` builds the CLI with system FFmpeg, libjxl, and Brotli. Initial GCC 13 checks on WSL Ubuntu 24.04 compiled the CLI parser and checked all remaining sources without warnings using package headers. At that stage packages were not installed, so full linking/execution was unverified. That Ubuntu provided libjxl 0.7.0 rather than Windows 0.11.2, potentially changing compression sizes. `validate_native.py` assumes Windows executables and DirectShow; do not use it for Linux.

## Permanent local installation: 2026-11-05

Added `native/install.cmd` and `install.ps1`. Because the checkout drive may be removable, installation copies tools/decoder to `%LOCALAPPDATA%\MJXL`, registers per user, and clears the filter cache. FFmpeg DLL imports are followed to copy only required avcodec/avformat/avutil/swresample libraries, totaling approximately 132 MB. Uninstall removes manifest-recorded files only.

Moved registration from W: to the local copy. Confirmed registration path, automatic filter selection, playback/seeking, PotPlayer local-DLL loading, CLI encode/verify/original recovery with system PATH only, and fresh thumbnails. Physical drive removal and another-PC installation were not tested then. Thumbnails on that PC used the Program Files MFT. Rerun install after rebuilding.

## Linux CLI execution validation: 2026-11-05

Installed required development packages in WSL2 Ubuntu 24.04 x86-64 and compiled/linked the complete CLI without warnings using GCC 13, libjxl 0.7.0, FFmpeg 6.1.1, and Brotli 1.1.0. It links system shared libraries. No Windows filters or Linux playback plugins were installed.

`native/validate_cli.py` ran seven samples with both PCM and FLAC (14 encodes), verify, and restore-original. Every restored source matched SHA-256. Each mode contained 1,332 frames; reports are in `results/linux-validation/cli-report.json`. The first FLAC file also passed MJPEG AVI restoration and RGB decoding. Linux verified/restored a Windows FLAC AVI, and Windows verified the first Linux FLAC AVI. This did not cover every cross-platform combination or Linux player playback.

Linux build/use guidance was added to the English README and native guide. The ignored executable is `dist/linux/mjxl`; its distribution-specific shared-library dependencies prevent treating it as a universal binary.

## Linux FFmpeg and ffplay: 2026-11-05

Installed Ubuntu 24.04 FFmpeg 6.1.1. Explicitly selecting its libjxl input decoder decoded all video/audio in seven PCM and seven FLAC AVIs without errors, 1,332 frames per mode. Reports are in `results/linux-validation/ffmpeg-report.json`.

ffplay with `-vcodec libjxl` reached the end of the first PCM and FLAC samples with SDL dummy video/audio outputs. Actual Linux display, listening, and seeking were not verified. FFmpeg MJXL FourCC mapping and GStreamer support were not changed. Documentation records both commands and validation limits.
