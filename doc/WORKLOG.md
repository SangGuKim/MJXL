# Work log

**English** | [Korean](WORKLOG.ko.md)

## 2026-11-05: CMake entry point and repository layout

The working checkout is `P:\tool\mjxl`, obtained from the remote repository instead of the USB checkout at `W:\proj\mjxl`. Earlier implementation and validation records are in [STATUS.md](../STATUS.md). The user confirmed that clearing the filter cache resolved the earlier PotPlayer codec-selection problem; do not treat it as an unresolved decoder defect.

### Problem and changes

The tracked root `MJXL.sln` referenced projects generated in the ignored `_build/vs2026` directory. A fresh checkout therefore had a solution with missing projects. The CMake entry point was hidden under `native`, and the English/Korean READMEs described different functionality.

- Added root `CMakeLists.txt` to connect the Windows native project. Linux retains its Makefile path.
- Excluded generated solutions/projects from Git. Preserved the user-generated local root solution and removed only its Git tracking. New solutions are generated inside the build directory.
- Changed the default directory to `_build/windows-vs2026`, preserving the old native-source cache. Caches from another source directory are rejected and are not deleted.
- Added `build.ps1 -ConfigureOnly` for configuration and solution generation without project compilation. Normal execution still builds all Release targets and copies runtime files, with explicit progress messages.
- Aligned the two root READMEs and moved detailed Python experiments into separate documentation.

### Validation

- The root configuration and `-ConfigureOnly` completed successfully.
- All 20 generated project paths exist; configuration did not compile project sources (CMake compiler checks are separate).
- The solution generator preserved the existing local root solution unchanged.
- Reusing the old native-source cache was rejected without modifying it.
- Built the small `mjxl_graph_test` target from the new entry point; did not rebuild the complete codec or reinstall it.
- Both root READMEs have matching executable examples and valid local links. UTF-8/LF and Git whitespace checks passed.

```powershell
.\native\prepare_deps.ps1
.\native\build.ps1 -ConfigureOnly
# Build all targets only when needed.
.\native\build.ps1
# Installation is performed by the user.
.\native\install.cmd
```

The generated solution is `_build/windows-vs2026/mjxl.sln`. `FFMPEG_SDK` takes precedence; `FFMPEG_PATH` is also supported. Existing build trees and installed binaries were not deleted or reinstalled during the layout update.

## 2026-11-05: English as the default public language

The user requested English comments, explanatory strings, script messages, and default documentation for public-source distribution. This overrides the earlier Korean C/C++ comment convention for this task. Preserve Korean documents as `.ko.md` companions. Third-party dependencies, Git internals, generated files, and private inputs are outside the source translation scope.

Keep default documents free of Korean text, including language-navigation labels. Default report generation is also English. Check all repository-owned text files, including `.gitignore`, instead of checking only C++ sources. Do not change algorithms or measurement values during translation.

Validation scanned all 51 repository-owned text files, including hidden configuration files. Korean text remained only in seven intentional `.ko.md` companions. C++ non-comment content was identical, and Python changes were limited to strings/comments. Python and PowerShell syntax, document links, recorded numeric values, and an English-report fixture passed. `-ConfigureOnly` completed with English progress messages. Modified files retain UTF-8 without BOM and LF. No installation scripts, certificate operations, or full codec rebuild were run for translation.

## 2026-11-05: Reported periodic PCM playback noise

The user heard periodic clicking in the encoded AVI in PotPlayer; the original MOV sounded clean. The input/output files are `samples/IMG_20040402_034913.MOV` and `.avi`. Encoding used only `encode -e 8`, so audio took the default PCM path.

Read-only checks found:

- 1,278 video frames, 15 fps, 85.2 seconds.
- Both audio streams are unsigned PCM8, mono, 8,040 Hz.
- Both contain 685,008 PCM bytes with SHA-256 `d8ceba93e29cbac7b62b83f61b3740e14d99236a57e05a6bceaeb8bdbb2c1b21`.
- The current CLI passed exact JPEG/PCM verification and complete original-file reconstruction.

Matching PCM bytes do not establish artifact-free output through the player. The playback issue is unresolved; no audio algorithm or muxer change has been made. Lossless controls were created in `results/audio-diagnostics`:

- `IMG_20040402_034913-restored-mjpeg.avi`: original JPEG/PCM restored with the native AVI writer.
- `IMG_20040402_034913-ffmpeg-mjpeg.avi`: original streams remuxed to AVI by FFmpeg.
- `IMG_20040402_034913-pcm.wav`: the identical PCM stream in WAV.

The user was asked to compare these three files with the same PotPlayer settings. Their listening results are needed to distinguish container/splitter behavior from the MJXL playback path. Do not mark this issue fixed based solely on the byte/hash checks.

### Listening follow-up and AVI-header controls

The user confirmed that the WAV is clean, while the original MJXL AVI clicks from the beginning and clearly around 27 seconds, as if interrupted every few frames. The native-restored MJPEG AVI also clicks. Both FFmpeg-remuxed AVI controls sounded clean. This points toward the native AVI-writing path rather than a change in stored PCM or a problem unique to JXL decoding.

The native file has AVI main flags `0x10` (index present), without `AVIF_ISINTERLEAVED`, despite interleaved chunks. Its PCM suggested buffer is 65,536 bytes, while actual audio chunks alternate between 1,024 and 584 bytes (together 0.2 seconds, or three video frames). FFmpeg records flags `0x910` and a 1,024-byte audio buffer. These differences are candidates, not proven causes.

Created three header-only MJXL controls with unchanged PCM hashes: `-test-interleaved.avi` adds `0x100`; `-test-audio-buffer.avi` sets the suggested audio buffer to 1,024; `-test-both.avi` applies both changes. User listening comparison is pending. The originals and source muxer have not been changed.

The first FFmpeg controls had different video packet/time-base behavior, so additional `-ffmpeg-mjpeg-cfr.avi` and `-ffmpeg-mjxl-cfr.avi` controls were created with 15 fps / 1,278 frames / 85.2 seconds. JXL stream copy required disabling parsing with `-fflags +noparse+nofillin`; otherwise this SDK merged independent JXL packets. The CFR MJXL control passed exact JPEG/PCM verification with `--no-original`; remuxed diagnostic files do not retain original-file recovery data.

The user reported that all three small header controls still click, as does native-restored MJPEG. Both CFR FFmpeg files are normal. The initial non-CFR FFmpeg JXL control has a still picture with normal audio and must not be used to validate video playback.

A section-swap experiment localized the difference to the AVI header: `-test-ffmpeg-header.avi` uses the normal FFmpeg `hdrl` with native media chunks/index and sounds normal; `-test-ffmpeg-layout.avi` uses native headers with FFmpeg media chunks/index and still clicks. Both passed exact JPEG/PCM and complete original-file recovery. Therefore native chunk placement alone does not explain the issue.

Two narrower controls were then created: `-test-audio-header.avi` replaces only the audio stream header/list; `-test-main-video-header.avi` replaces the main and video headers. Both passed exact recovery and have the same PCM hash. The native/reference audio header differences are handler 0 versus 1, suggested buffer 65,536 versus 1,024, and PCM `strf` length 18 versus 16; buffer-only changes already failed.

### Cause and fix

Further listening results, all with the unchanged PCM hash:

| Control | Change from the native AVI | Result |
| --- | --- | --- |
| `-test-main-video-header.avi` | FFmpeg main and video headers | Clicks |
| `-test-audio-header.avi` | FFmpeg audio header and list | Clean |
| `-test-only-handler.avi` | Audio `fccHandler` 0 to 1 | Clicks |
| `-test-only-junk.avi` | 4,120-byte `JUNK` in the audio `strl` | Clicks |
| `-test-only-strf16.avi` | `strf` 18 to 16 bytes; `movi` moves to offset 312 | Clean |
| `-test-strf18-aligned.avi` | 18-byte `strf`; `movi` at 4,440 (4-byte aligned) | Clicks |
| `-test-strf16-unaligned.avi` | 16-byte `strf`; `movi` at 4,442 (not aligned) | Clean |

The last two controls rule out `movi` alignment, which the `strf` change had also altered. The cause is the 18-byte PCM `strf` with `cbSize` 0. Why PotPlayer reacts to it is unknown; its internals were not inspected.

`AviWriter` now writes a 16-byte `PCMWAVEFORMAT` for PCM. The FLAC header is unchanged because it needs the extended form. The user confirmed that `-fixed-pcm.avi`, newly encoded with `encode -e 8`, sounds clean.

The four samples currently in `samples/` passed PCM and FLAC encode, verify, and original-file SHA-256 checks. Their PCM AVIs also passed DirectShow playback and seeking, local Media Foundation tests, and FFmpeg audio decoding (`results/windows-strf16-validation`). Listening covered only `IMG_20040402_034913`, 8-bit mono PCM. AVIs encoded earlier keep the old header until re-encoded.

## 2026-11-05: FLAC as the default audio storage

The user reported that FLAC plays without problems and asked to make it the default, with a separate switch for keeping the source audio. `encode` now stores audio as FLAC by default. `--copy-audio` copies it unchanged and replaces `--flac`, which is now rejected as an unknown option. The name avoids `--pcm` because it describes the action rather than a source format. Input audio still has to be PCM, or FLAC with MJXL metadata; other codecs are rejected in both modes. Original 32-bit PCM still remains PCM.

The four samples passed encode, verify, and original-file SHA-256 checks in both modes. Both sets of AVIs also passed DirectShow video playback and seeking and FFmpeg audio decoding (`results/windows-copy-audio-validation`). The default output contains `flac` audio and the `--copy-audio` output contains `pcm_u8`. Default sizes for `IMG_20040402_034913` were 22,320,444 bytes with FLAC and 22,592,870 bytes with copied audio at effort 7.

Earlier records still apply to FLAC AVI: Windows has no built-in DirectShow decoder for it, and Media Foundation AVI sources stall on it. Use `--copy-audio` for those paths. Fresh Explorer thumbnail generation was not retested with the new default.

### Follow-up review and continuation notes

- Confirmed local HEAD and remote `main` at `6af9d708879d183a96b079aabfffe6e3021b1085`; the checkout was clean before this note. Do not revert the intentional FLAC default or reintroduce `--flac` in examples. Historical entries describe older behavior.
- Independently reran the current CLI's `verify` on all eight outputs in `results/windows-copy-audio-validation` (four originals, PCM and FLAC). Exact JPEG/PCM and complete original-file verification passed; source SHA-256 and output sizes also matched the report. This review did not repeat encoding or audible playback.
- Audited 51 tracked text files: Korean appeared only in `.ko.md` companions, and local Markdown links resolved. The pushed commit passed Git whitespace checks.
- Future muxer regression checks should use fresh output directories. `validate_native.py` verifies an existing AVI instead of re-encoding it; passing verification alone does not prove that its header was written by the current muxer.
- Original-file recovery stores the layout table and all non-media bytes separately compressed with Brotli at maximum quality in `mjxo`. This includes container metadata and padding, not just the MOV header. JPEG/PCM payloads are reconstructed from the media streams instead of duplicated; the original SHA-256 validates the result.

## 2026-11-05: Component statistics and project motivation

Encoding now ends with a byte-count table for audio, video, container/recovery data, and the total file, showing original size, stored size, and reduction. Stream rows count packet payloads; the remaining row accounts for all container overhead and reconstruction data so totals reconcile. Negative reductions indicate growth, and zero-byte baselines display `n/a`.

The root English and Korean READMEs introduce MJXL as Motion JPEG XL and connect JPEG XL's reversible JPEG recompression to complete original-file preservation. They explain why modest savings can be useful for an archive, distinguish this goal from lossy H.265 viewing copies and pixel-only losslessness, and identify GStreamer integration and macOS playback as future work rather than existing support.

Built the Windows CLI and tested a real 63-frame MOV with default FLAC and `--copy-audio`, plus a video-only AVI. Compared every reported byte count and percentage against independent FFprobe input packet totals and output RIFF chunk totals. All three outputs also passed exact original-file verification. Test outputs are in `C:/Temp/mjxl-stats-validation`; effort 1 was used for this check, not as a compression benchmark. Installed binaries were not updated. No commit or push was performed.

## 2026-11-05: Experimental MKV output using FFmpeg

`encode` now selects AVI or Matroska from the final output extension, case-insensitively. Other output extensions are rejected instead of writing disguised AVI. MKV uses FFmpeg's existing Matroska muxer with `V_MS/VFW/FOURCC` and `MJXL`; no additional library is required. Original-file reconstruction data is an attachment named `mjxl-original.bin`, with MIME type `application/x-mjxl-original`. FLAC's original PCM depth uses the audio track tag `MJXL_ORIGINAL_BITS`. AVI keeps its existing `mjxo` and `mjxa` representation. `verify`, `restore-original`, and `bench` accept MKV; `restore` writes only MJPEG AVI and now checks that output extension.

The reader accepts the dedicated recovery attachment and rejects unrelated additional streams. Duplicate recovery attachments and missing reconstruction data are rejected by complete-original recovery. `--no-original` requires the same explicit verification opt-in as AVI. The validation script accepts `--container avi|mkv` (default AVI); reports now use the container-neutral `encoded_bytes` field and include `container`.

Validation on Windows:

- `results/matroska-v1`: four actual MOV sources, each with copied PCM and default FLAC, passed encode, JPEG/PCM verification, physical original-file restoration with SHA-256 comparison, and FFmpeg full video/audio decoding. MJPEG AVI restoration and RGB decoding also passed.
- `results/matroska-edge-v1`: generated video-only and 16/24/32-bit stereo sources passed both audio modes, exact restoration, MJPEG AVI restoration, and FFmpeg decoding. PCM32 remained PCM. Uppercase `.MKV`, no-original refusal/opt-in, unsupported extensions, output overwrite refusal, old AVI inputs, and new AVI outputs passed.
- The user confirmed PotPlayer video/audio playback and seeking on both 4.2-second MKVs. The user clarified that Explorer **thumbnails**, not the Preview pane, were absent. Preview was not tested. No thumbnail provider, registry registration, codec installation, or thumbnail cache change was made.
- The four samples' MKVs were 970–21,638 bytes larger than same-effort AVI outputs, despite identical media payloads. For `IMG_20040402_034913` at effort 7: copied PCM AVI 22,592,870 bytes versus MKV 22,605,136; FLAC AVI 22,320,444 versus MKV 22,340,768. MKV overhead is not assumed smaller.

The CLI is rebuilt in `dist/Release`; the installed local copy was not updated. Linux compilation of the MKV path, large MKV files, and DirectShow/MFT MKV graph tests were not performed. Existing graph tests explicitly use AVI Splitter and must not be cited as MKV validation. Future thumbnail options include a dedicated MJXL shell thumbnail provider, or JPEG cover attachments plus a supporting provider such as Icaros; neither was implemented here. No commit or push was performed.

## 2026-11-05: MOV feasibility and end-of-session handoff

The user asked whether MOV can carry MJXL. A diagnostic remux with the installed FFmpeg SDK succeeded for MJXL video tagged `MJXL` and copied PCM8 audio. The validated file is `C:/Temp/mjxl-mov-pcm-check.mov` (2,575,893 bytes), sourced from `results/windows-copy-audio-validation/pcm/IMG_20040318_230535.avi` with the parser disabled. It passed `mjxl verify samples/IMG_20040318_230535.MOV C:/Temp/mjxl-mov-pcm-check.mov --no-original`: 63 exact JPEG frames and 33,768 exact PCM bytes. FFmpeg full video/audio decoding also passed. The video is 15 fps with time base 1/15360, and audio uses 1/8040. PotPlayer playback and Explorer thumbnails for this MOV were not tested.

The diagnostic command, with SDK and checkout paths adjusted as needed, was:

```powershell
& "$env:FFMPEG_PATH/bin/ffmpeg.exe" -v error -n -fflags +noparse+nofillin -c:v libjxl -i results/windows-copy-audio-validation/pcm/IMG_20040318_230535.avi -map 0:v:0 -map 0:a:0 -c copy -tag:v MJXL -f mov C:/Temp/mjxl-mov-pcm-check.mov
```

This is only a feasibility file: the remux omitted original-file recovery data. Do not use it as a complete original-file archive or delete the source based on its media-only verification. An earlier remux from the MKV control (`C:/Temp/mjxl-mov-feasibility.mov`) retained rounded millisecond timestamps and failed the CLI's constant-frame-rate check; do not confuse it with the validated AVI-sourced file. The FLAC-to-MOV attempt (`C:/Temp/mjxl-mov-flac-feasibility.mov`) failed at header writing with `flac only supported in MP4`; it is not a usable result. None of these diagnostic files were deleted.

MOV output is **not** implemented in the CLI: `.mov` remains rejected. Future implementation needs MOV writing with copied PCM, a defined private atom for recovery data plus read/verify/restore support, and playback/thumbnail tests. Do not infer QuickTime or macOS playback support from successful muxing or forced FFmpeg decoding. MP4 support was not established by these tests.

At the user's request, stop here for today. The checkpoint is remote/local `main` commit `6af9d70` plus uncommitted changes to 10 tracked files: root READMEs, native READMEs, both work logs, `native/encoder.cpp`, `native/cli.cpp`, `native/original_file.hpp`, and `native/validate_cli.py`. These include component statistics, project motivation/roadmap, and experimental MKV support. The CLI in `dist/Release` was rebuilt; the installed copy was not updated. No installation, commit, push, or cleanup deletion was performed. Preserve these changes and the ignored sample/results/build files when resuming. Next steps, when requested, are MOV support and/or a dedicated Explorer thumbnail path; neither is authorized to continue automatically after this stop.

### Follow-up: requested source checkpoint

The user subsequently requested committing and pushing today's source and documentation so work can resume on another PC. The preceding uncommitted/no-push status describes the state before that request. This checkpoint contains source and documentation only; samples, diagnostic media, dependencies, and build outputs remain local and ignored. On another PC, fetch/pull `main`, prepare dependencies, and rebuild before installing. Do not assume the `C:/Temp` MOV diagnostics or ignored `results` files are available there. No further implementation is requested tonight.

## 2026-11-06: Portability implementation, first increment

Implemented four-way accounting (audio, video, recovery payload, pure container), with total checks in validate_cli.py and a fixture SHA-256 manifest in native/testdata. The archive payload format remains version 1. Replaced libavutil SHA-256 with unmodified vendored PicoSHA2 (MIT, pinned provenance) and replaced FFmpeg FLAC encoding/decoding with libFLAC 1.5.0, including callback exception containment, final STREAMINFO and legacy PCM8 representation. Decoder frame CRC failures and truncated packets fail rather than returning partial audio. Whole-file verification continues to check original SHA-256.

Split CLI orchestration from container.cpp and introduced container.hpp without FFmpeg types. CMake now has mjxl_core, mjxl_audio, mjxl_archive and mjxl_container boundaries, optional Windows adapters, optional portable tests, and a system-package branch for Linux/macOS. The last branch has not been built on those platforms. FFmpeg remains required by the transitional CLI container backend. This increment does not implement MOV/ALAC, GStreamer, native container replacements, or macOS apps/extensions.

Added --audio-codec auto|flac|alac|copy with --copy-audio compatibility. Auto retains FLAC for supported AVI/MKV PCM formats and copies PCM32. Explicit unsupported FLAC formats, conflicting options, and the unimplemented ALAC backend fail; the latter does not create output files. No experimental AVI ALAC or optional external-FFmpeg backend is exposed yet.

Validation on Windows x64 / VS2026 v145:

- Full Release build passed, including existing DirectShow/MFT targets; this is a build check, not a new playback/registration check. Built files are in dist/Release; no installation or registration was performed.
- Separate MJXL_BUILD_CLI=OFF / MJXL_BUILD_WINDOWS_ADAPTERS=OFF build and CTest passed. The test covers 54 FLAC cases (PCM8/16/24, 1/2/8 channels, 8040/44100/48000 Hz, 1/5003 samples), damaged/truncated packets, policy and CLI conflicts, SHA-256 known answers, and archive integrity. dumpbin shows only KERNEL32.dll for the test executable, with no FFmpeg dependency.
- Seven real camera MOV files passed 28 fresh encodes (AVI/MKV times PCM/FLAC), media verification, physical original restoration and SHA-256 comparison, four-component sum checks, MJPEG restoration and a first-sample RGB bench in each container suite. Effort 1 was used for regression speed, not a compression benchmark. Reports are in results/portability-v1-avi and results/portability-v1-mkv.
- Seven historical FFmpeg-FLAC AVI archives passed complete verification against their MOV originals.
- Independent FFmpeg decoding matched normalized original PCM and decoded video for all 28 new outputs. AVI/MKV video and audio packet payloads matched in all 14 pairs. Unavailable/invalid/conflicting codec requests were rejected without output creation. Summary: results/portability-crosscheck.json.

Library preparation now pins libFLAC at 1507800de4b70e21be71f38caa0d9079d0bc6e45; build packaging copies FLAC and PicoSHA2 licenses. The MSVC libFLAC target force-includes stdlib.h to supply missing allocation declarations in upstream win_utf8_io.c without editing downloaded sources. No commit or push was performed.

## 2026-11-06: ALAC, native MOV and container comparison

Before this increment, committed and pushed 5238327 with author/committer peperu <peepru@outlook.com> as requested. Push initially used the company SSH identity and failed; a repository-local core.sshCommand now selects the pre-existing personal key, with global SSH settings unchanged. The ALAC work below is subsequent uncommitted work.

Implemented Apple ALAC encoding/decoding pinned to c38887c5c5e64a4b31108733bd79ca9b2496d987. PCM8 promotion preserves amplitude; PCM16/24/32 and supported speaker layouts use reversible channel permutation. Known unsupported speaker layouts fail for explicit ALAC and fall back to PCM in MOV auto. Generated checked source copies add frame/input bounds and the zero-shift predictor correction; original downloaded files are untouched. libFLAC remains the AVI/MKV default and legacy reader.

Native MOV writing now stores independent MJXL video, ALAC or PCM, exact rational video timing, co64, and private moov/udta MJXO/MJXA atoms for complete original recovery. MKV supports explicit ALAC via its existing FFmpeg writer. AVI ALAC uses private WAVE tag 0xA1AC behind --audio-codec alac --experimental-avi-alac; this is not a standard mapping. Native verification passes but external FFmpeg needs forced -c:a alac audio decoding. Do not infer AVI Splitter or PotPlayer support. CLI media reading and MKV writing still require FFmpeg.

Windows Release build and standalone CTest passed: 54 FLAC cases and 40 ALAC depth/channel/length cases plus configuration/truncation and policy checks. The camera suite in results/alac-v2-camera covers seven real inputs and 56 codec/container combinations. The final synthetic suite in results/alac-v4-synthetic covers 16/24/32-bit stereo and 5.1-channel input in 30 successful combinations; two explicit PCM32 FLAC refusals are expected. Each suite checks exact physical original restoration, external normalized PCM, video decoding and identical media packet digests across containers, plus MJPEG restoration from ALAC. The earlier incomplete synthetic run in results/alac-v2-synthetic stopped at the expected PCM32 FLAC refusal; it is superseded, not a completed validation report.

Additional checks in results/alac-v5-edges passed for 96 kHz ALAC, quad auto fallback/explicit refusal, video-only AVI/MKV/MOV, seek, no-original policy, malformed atom sizes and duplicate recovery atoms. The earlier results/alac-v4-edges fixture attempt stopped on a diagnostic FFmpeg duration argument before encoding and is not a validation result. No diagnostic files were deleted.

For the seven PCM8 camera inputs, raw audio totals 556,392 bytes, FLAC 374,249 (-32.74%), and ALAC 939,327 (+68.82% over raw; +150.99% over FLAC). Within MKV, complete archives total 54,399,041 bytes for FLAC and 54,962,913 for ALAC (+1.04%). AVI/MKV therefore retain FLAC defaults. MOV retains its Apple-oriented ALAC default, with --copy-audio producing smaller MOVs on these particular inputs. See AUDIO-CONTAINERS and MOV-ALAC-PROFILE in both languages for measurements and file conventions.

The built CLI is in dist/Release and ALAC's license is copied by the build script. The Linux Makefile now delegates to common CMake; the ALAC/MOV revision was not built on Linux/macOS. No installation/registration, physical QuickTime/PotPlayer playback, GStreamer or Finder validation was performed. No additional commit or push was made after 5238327.


## 2026-11-06: Follow-up MOV playback diagnosis

Use only outputs derived from W:/proj/mjxl/samples for diagnosis; do not directly access the user's K: drive. Stopped relying on PotPlayer's existing playlist and opened explicit W: paths.

PotPlayer playback information showed working ALAC through Built-in Audio Codec/Transform, but no video decoder. File information listed the MJXL video track; the running graph contained only Built-in MP4 Source, the audio decoder and WASAPI renderer. Diagnostic FFmpeg-remuxed MOVs with either ALAC or PCM also lacked video. Keep audio compression behavior separate from MOV video routing. The user confirmed AVI/MKV+FLAC playback, and the AVI automatic DirectShow playback/seek control passed.

A temporary reversed-FOURCC registration did not help. Removed that source change and restored/re-registered the original installed decoder, checking its SHA-256 against the backup; restored the installation file list. Diagnostic remuxes omit original-file recovery data and are not archival outputs.

The user confirmed visible playback of the W: MJXL MOV+ALAC using ffplay with explicit -vcodec libjxl; exit code was zero. A warning about automatic identification of the MJXL sample entry may remain. This path does not use MFT.

```powershell
& C:/sdk/ffmpeg/bin/ffplay.exe -vcodec libjxl W:/proj/mjxl/results/alac-v2-camera/mov/alac/IMG_20060430_203434.mov
```

The user reports Explorer thumbnails for MJPEG MOV and MJXL AVI, but not MJXL MOV. A shared cause between PotPlayer and the thumbnail handler has not been established. MFT is not installed; DirectShow registration alone does not establish MOV thumbnail support. macOS MediaExtension routing, any necessary Format Reader, QuickTime playback and Finder thumbnails remain separate future gates.

Discussed comparing actual compressed audio size with PCM in auto mode and keeping PCM when compression grows it. This is not implemented yet: MOV auto currently uses format/layout capability only. Explicit ALAC selection should remain honored.
