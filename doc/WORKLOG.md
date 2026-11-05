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
