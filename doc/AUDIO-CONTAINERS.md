# ALAC, FLAC and container comparison

[한국어](AUDIO-CONTAINERS.ko.md)

Measured 2026-11-06 on Windows x64 / VS2026 v145, using libjxl 0.11.2, libFLAC 1.5.0 compression level 8 and Apple ALAC c38887c5 with its default encoder and 4096-frame packets. JXL effort 1 reduces regression time; it does not control audio compression. Results describe these inputs and implementations, not universal codec rankings.

## Camera inputs

Video/audio packet payloads matched across containers for each codec on all seven inputs. The table counts MKV audio packet bytes, excluding container headers, codec configuration and recovery payload.

| Input | PCM bytes | FLAC bytes | ALAC bytes | ALAC growth over FLAC |
| --- | ---: | ---: | ---: | ---: |
| IMG_20040318_225819.MOV | 15,008 | 11,605 | 26,795 | 130.89% |
| IMG_20040320_230212.MOV | 89,512 | 59,994 | 149,755 | 149.62% |
| IMG_20040321_000945.MOV | 42,344 | 30,579 | 73,382 | 139.98% |
| IMG_20040322_025026.MOV | 97,016 | 79,668 | 177,866 | 123.26% |
| IMG_20040325_190603.MOV | 70,752 | 50,996 | 123,535 | 142.24% |
| IMG_20040326_055048.MOV | 85,760 | 60,880 | 147,755 | 142.70% |
| IMG_20060430_203434.MOV | 156,000 | 80,527 | 240,239 | 198.33% |
| Total | 556,392 | 374,249 | 939,327 | 150.99% |

For these PCM8 inputs, FLAC reduced raw audio by 32.74%, while ALAC grew it by 68.82%. ALAC reversibly promotes PCM8 to PCM16 while preserving playback amplitude. The FFmpeg ALAC encoder produced 26,793 payload bytes on the first input, just two bytes below the Apple implementation's 26,795.

| Storage path | Total file bytes | Pure container bytes |
| --- | ---: | ---: |
| AVI / FLAC | 54,374,462 | 42,118 |
| MKV / FLAC | 54,399,041 | 66,697 |
| MKV / ALAC | 54,962,913 | 65,491 |
| MOV / COPY | 54,550,271 | 35,784 |
| MOV / ALAC | 54,922,942 | 25,520 |

Within MKV, ALAC increased complete archive size by 563,872 bytes (1.04%) over FLAC. Totals include original-file recovery payloads. Container savings did not offset the audio difference. MOV defaults to ALAC for the Apple interoperability path; for these PCM8 sources, --copy-audio produces smaller MOVs. AVI/MKV retain FLAC defaults.

## Synthetic inputs

These deterministic 0.4-second multisine signals are edge fixtures, not representative recording benchmarks. They accompany 30000/1001 fps MJPEG.

| Input | PCM bytes | FLAC bytes | ALAC bytes |
| --- | ---: | ---: | ---: |
| pcm16-stereo.mov | 70,560 | 13,385 | 15,306 |
| pcm16-surround.mov | 230,400 | 44,216 | 52,360 |
| pcm24-stereo.mov | 115,200 | 23,792 | 54,664 |
| pcm32-stereo.mov | 153,600 | — | 93,051 |

Explicit PCM32 FLAC requests are rejected by the current profile; auto keeps PCM. ALAC restored PCM32 exactly. The 5.1 test also matched FFmpeg's normalized PCM, validating channel permutation beyond internal round trips.

## Interoperability

| Path | Native original-file restoration | FFmpeg automatic audio identification | Notes |
| --- | --- | --- | --- |
| MOV / ALAC, PCM | Passed | Passed | Native MOV writer; physical QuickTime Player validation is separate. |
| MKV / ALAC, FLAC, PCM | Passed | Passed | MKV writing still uses FFmpeg. |
| AVI / FLAC, PCM | Passed | Passed | Existing defaults remain. |
| AVI / experimental ALAC | Passed | Failed | Forced -c:a alac produces matching PCM; not promoted to default. |

The suite covered 56 real-file and 30 synthetic codec/container combinations with exact original restoration, external PCM comparison and cross-container media digests. Two explicit PCM32 FLAC refusals are excluded from successful encode counts. Experimental AVI requires --audio-codec alac --experimental-avi-alac. PotPlayer, Windows AVI Splitter, GStreamer, macOS, QuickTime Player and Finder were not tested here. See the [MOV/ALAC profile](MOV-ALAC-PROFILE.md) for the file convention.


## Reproduction

```powershell
python -X utf8 native/validate_audio_containers.py --binary dist/Release/mjxl.exe --ffmpeg C:/sdk/ffmpeg/bin/ffmpeg.exe --ffprobe C:/sdk/ffmpeg/bin/ffprobe.exe --samples samples --output results/new-audio-comparison --effort 1
```

Use the sgtk Python environment and a fresh output directory. Inputs and encoded media are ignored local artifacts; reports are JSON in the chosen output folder. Exact input hashes are recorded by the script.

Additional edge checks passed for 96 kHz ALAC, MOV auto PCM fallback and explicit ALAC refusal for quad, video-only files in all three containers, seeking, --no-original verification/restoration policy, invalid atom sizes and duplicate MJXO refusal. Linux/macOS execution and large files are not included.


## Follow-up playback check

PotPlayer plays ALAC from MOV but does not connect an MJXL video decoder; an FFmpeg-remuxed MOV with PCM behaves the same way. The user confirmed actual ffplay MOV+ALAC playback with -vcodec libjxl, without MFT. The user also reports missing Explorer thumbnails for MJXL MOV. A common cause between these paths is unproven. Successful FFmpeg checks above do not establish PotPlayer or thumbnail support.
