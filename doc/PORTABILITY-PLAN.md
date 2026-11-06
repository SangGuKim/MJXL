# MJXL portability and container implementation plan

English | [Detailed Korean design](PORTABILITY-PLAN.ko.md)

Date: 2026-11-06. Status: design under incremental implementation; see the checkpoint below. This companion summarizes the detailed Korean design and its delivery gates. Target and API names below are proposals.


## Implementation checkpoint: 2026-11-06

The first increment implements accounting, initial library boundaries, and independent FLAC/SHA-256 backends. The CLI container backend still requires FFmpeg. MOV/ALAC, native AVI/MOV readers, independent MKV, GStreamer, and macOS apps/extensions remain unimplemented.

- P0 adds four-way accounting and sum checks plus a [fixture hash manifest](../native/testdata/portability-fixtures.json); compare AVI/MKV media payloads at the same effort.
- P1 separates mjxl_core, mjxl_audio, mjxl_archive and mjxl_container, removing FFmpeg types from shared headers. The buffered Movie reader remains; timed PacketRef, streaming/spooling and the full Reader interface are pending.
- P2 uses libFLAC 1.5.0 callbacks and vendored PicoSHA2, retaining historical recovery payloads and PCM8 conventions. Validation is on Windows; Linux/macOS execution is pending.
- --audio-codec auto|flac|alac|copy and --copy-audio share one policy. Unimplemented ALAC fails without producing output; conflicting options and explicit unsupported FLAC formats also fail. AVI/MKV auto chooses FLAC or PCM copy for formats outside the current FLAC profile.
- CMake can build/test portable libraries without FFmpeg. The new Linux/macOS system-library branch is not yet validated on those platforms.


## Goals and compatibility

Remove FFmpeg from the required CLI/archive dependency graph. Keep GStreamer optional and FFmpeg available both as an independent development oracle and as an explicitly selected optional runtime backend. Preserve three separate guarantees: exact JPEG reconstruction, exact PCM bytes, and complete original-file SHA-256 verification. Playback alone establishes none of the archive guarantees.

Continue reading existing AVI mjxo/mjxa, MKV recovery attachments and MJXL_ORIGINAL_BITS, and the existing PCM8-in-16-bit-FLAC convention. Reuse the version 1 recovery payload across containers. Retain output overwrite refusal, partial files, validation before finalization, and explicit --no-original behavior.

Initially support one CFR video track and at most one audio track. Reject unsupported extra tracks, external media references, encrypted media, fragmented files, VFR, and unsupported edit timelines before conversion. Unknown container metadata remains part of original-file recovery rather than silently disappearing.

## Current architecture and target boundaries

The libjxl core and Windows playback adapters are already independent of FFmpeg. encoder.cpp combines demuxing, native AVI writing, FFmpeg MKV writing, CLI orchestration, and verification. flac_audio.cpp uses libavcodec; original_file.cpp uses libavutil SHA-256. Root CMake is Windows-only and Linux uses a separate Makefile. The classic AVI writer is limited to less than 2 GiB, and whole-file/packet buffering also limits scale.

| Proposed target | Responsibility |
| --- | --- |
| mjxl_codec | libjxl JPEG reconstruction and RGB decoding |
| mjxl_audio | PCM representation and libFLAC/Apple ALAC conversion |
| mjxl_archive | Layout/residue, Brotli, portable SHA-256 |
| mjxl_container | Shared reader/writer contracts; AVI, MOV, MKV backends |
| mjxl CLI | Conversion, verification, restoration, accounting |
| gst-mjxl | Optional playback source and GstVideoDecoder adapter |
| Platform adapters | Existing DirectShow/MFT and new macOS app/extensions |

Do not expose AV*, Gst*, Windows, or Apple types in shared headers. Introduce Rational, TrackInfo, PacketRef, Reader, and Writer. Packets carry integer timestamps/durations with time bases, physical offsets/lengths, and sync status. Distinguish archive byte ranges from playback packet boundaries, particularly when regrouping PCM samples.

First preserve the existing buffering behind these interfaces. Then add seekable ByteSource, bounded buffers and temporary spooling for media, recovery residue, and large tables. Use checked 64-bit arithmetic; large offsets alone do not establish large-file support.

## Removing FFmpeg

Replace FLAC with libFLAC callbacks while preserving STREAMINFO, final sample count/MD5, channel order, exact PCM8/16/24 restoration, and the final short block. Keep PCM32 uncompressed initially. Benchmark compression again: FFmpeg level 12 is not a directly transferable libFLAC setting.

Vendor a small reviewed portable SHA-256 implementation with provenance/license and known-answer checks. Cross-read historical recovery payloads.

Implement classic AVI reading for bounded RIFF/LIST traversal, padding, stream headers, movi/rec lists, and idx1. Treat OpenDML/AVIX/extended indexes as a separate supported profile or reject them explicitly. Keep the existing writer limit until extended AVI support is complete.

Use libmatroska/libebml for MKV, retaining V_MS/VFW/FOURCC+MJXL, the recovery attachment name/MIME, and original-depth metadata. Implement timestamp, cluster, cues, and attachment glue; audit pinned dependency licenses and linkage.

Retain an optional development-only FFmpeg backend during comparison. Remove SDK discovery, default linkage, and DLL copying after replacement readers/writers pass compatibility gates.

## MOV profile and size accounting

Implement self-contained nonfragmented MOV. Write sequential media in mdat, then moov; support extended-size atoms and co64. Defer fast-start relocation. Use stsd/stts/stsc/stsz/stco or co64; independent video samples are all sync samples. Compress CFR timing into stts runs. Represent fps=num/den with track timescale=num and sample duration=den where representable, using checked integer conversion between track/movie time bases. Do not inherit rounded MKV timestamps as original timing.

Use experimental MJXL sample entries containing one complete independent JXL frame per sample. Specify color, aspect ratio, and orientation policy across adapters. This is not a standardized JPEG XL video mapping.

MOV defaults to ALAC and offers explicit PCM copy. Inventory camera PCM formats and use sample-aligned chunk grouping. The policy below supersedes the earlier PCM-first/FLAC-MOV feasibility policy.

Propose versioned MJXO under moov/udta for the unchanged recovery payload and MJXA for auxiliary audio information. Freeze atom placement, byte order, bounds, and duplicate handling in a format specification before implementation. Do not copy lowercase AVI chunk names into MOV as if they were registered codes. External remuxers may discard private atoms; always distinguish playable media from a complete archive.

The MOV reader must validate nested atom bounds and sample tables, physical sample ranges, 32/64-bit lengths, and unsupported timelines. Begin with own output and target camera files; explicitly reject compressed moov, external references, unsupported edit lists, and fragmented files.

Report:

`total = video payload + audio payload + recovery payload + pure container`

Wrappers belong to pure container; original inputs have zero recovery payload. Compare identical JXL, audio, and recovery payloads across AVI/MKV/MOV. Separately report source MOV, media-only output, and full archive output. Existing AVI adds an 8-byte chunk header, a 16-byte idx1 entry, and padding per packet. MOV chunk grouping may reduce this cost, but savings require measurement.

Historical MKV outputs were 970–21,638 bytes larger than equivalent AVI outputs. The successful MOV diagnostic omitted recovery data, so it is not a fair archive-size comparison. See the [work log](WORKLOG.md).


### Audio selection and optional FFmpeg

The default build and archive workflow must remain complete without FFmpeg. Its presence enables explicitly selected functionality rather than silently changing defaults. Proposed options are --audio-codec auto|alac|flac|copy and --backend native|ffmpeg; retain --copy-audio as a compatible alias and reject conflicts.

| Container | Initial auto policy | Explicit alternatives |
| --- | --- | --- |
| MOV | ALAC | PCM copy |
| MKV | FLAC pending ALAC interoperability results | ALAC, PCM copy |
| AVI | FLAC, or PCM copy for unsupported FLAC input formats | PCM copy; later experimental ALAC switch |

Centralize codec/container capabilities. Report codec/backend and selection reason. Explicit codec failure is an error, not silent fallback. Auto fallback applies to known format limitations, never corruption or encoding failures.

Use Apple's public ALAC encoder/decoder with its license. Reversibly map PCM8 into the upper byte of PCM16 and retain original depth; validate PCM16/24/32, channel order, configuration and final short packets. Retain libFLAC for existing archives and AVI/MKV. Investigate GStreamer ALAC playback without gst-libav and add a shared-codec adapter if needed.

Later expose AVI ALAC behind a dedicated experimental switch (proposed --experimental-avi-alac). Validate configuration/tag mapping, splitters, PotPlayer, native readers, GStreamer, seek and exact reconstruction. If successful, consider ALAC defaults across all containers; otherwise keep FLAC or PCM copy for AVI.

For the later optional FFmpeg backend, investigate an external ffmpeg executable before optional SDK linkage. Support explicit executable paths and discovery, version/encoder/muxer capability probing, argument-array subprocess invocation, cancellation, stderr/exit handling and partial-file cleanup. Missing executable/capability is a clear error. Explicit backend/codec selection may attempt combinations outside the native support table, but cannot guarantee overriding FFmpeg muxer restrictions.

Only lossless audio is allowed in archive mode. Keep video payloads unchanged rather than re-encoding JPEG. A plain remux that discards private recovery atoms/attachments is not an archive. Either feed encoded packets/configuration into shared writers or preserve/reinsert recovery data, then verify the final file's JPEG, PCM and complete original SHA-256. Lossy viewing copies are a separate future feature.

References: [Apple ALAC source/license](https://macosforge.github.io/alac/) and [supported PCM formats](https://github.com/macosforge/alac/blob/master/ReadMe.txt).

## GStreamer

Implement mjxldec as GstVideoDecoder, initially with proposed private video/x-mjxl caps, parsed=true, alignment=frame, dimensions, and frame rate. Validate format, decode through libjxl, negotiate RGB/RGBA, respect output stride, and finish frames with timestamps/durations. Handle flush, seek, EOS, cancellation, errors, and per-instance worker limits.

Demux mapping is a separate deliverable. Collect actual caps from avidemux/qtdemux/matroskademux and propose mappings for the experimental FOURCC/sample entry. A capsfilter cannot repair an upstream demuxer rejecting the track.

Provide an explicit local-file mjxlsrc URI source bin using shared readers while upstream mapping is unavailable. Expose packet pads and time seeking/query support. Do not give it priority over all ordinary MOV handlers. The initial application selects it explicitly; a pull-mode demux adapter can follow.

Target playback branches are source → queue → mjxldec → videoconvert → video sink and source → queue → PCM handling or flacparse/flacdec → audioconvert → audioresample → audio sink. This is a proposed graph, not a working command. Use one clock/segment and origin-based timestamp rescaling to avoid cumulative rounding drift.

Validate explicit pipelines first, then playbin/decodebin routing, seeking, and pause/resume. Test without gst-libav. Encoding/muxing and archive-preserving GStreamer remuxing are later work. This plugin does not automatically add support to VLC or IINA.

## macOS

First unify cross-platform CMake and isolate Windows/MSVC-only targets and flags. Check Apple Clang C++20 library requirements including std::format. Propose Apple Silicon arm64 and macOS 15+ as the first validation baseline; confirm the minimum OS by actual build/run. Intel/universal2 requires complete per-architecture dependencies and validation.

Deliver the archive CLI first, followed by a small GStreamer player with a private runtime/plugin/scanner deployment inside its app bundle. Validate library relocation, licenses, nested signing, hardened runtime, notarization, and quarantined launch on a clean Mac without system GStreamer. This is application playback, not system codec registration.

Add an app-hosted QLThumbnailProvider extension using the shared reader and one representative frame, with cancellation and resource bounds. Keep Quick Look Preview a separate target, initially a static frame and metadata. Shared .mov/.avi/.mkv UTIs do not guarantee codec-specific extension routing. Prove coexistence with normal media; if routing fails, ship app-local thumbnails first. A dedicated extension/UTType would require a separate proposal, not automatic file renaming.

For native integration, prototype a macOS 15+ MediaExtension decoder. First establish whether the built-in MOV reader routes MJXL samples to it. Write decoded frames into buffers allocated by MEVideoDecoderPixelBufferManager, respecting stride and color attachments; contain C++ lifetime/errors behind ObjC++ or a narrow C bridge. If routing fails or AVI/MKV needs native readers, investigate a separate Format Reader extension. Verify audio, entitlement, user activation, signing, and host policy. Record QuickTime Player, an AVFoundation test host, Finder, and professional apps independently. Do not promise OS-wide or hardware decoding support.

## Delivery gates

| Phase | Deliverable | Dependencies | Completion gate |
| --- | --- | --- | --- |
| P0 | Accounting, format profile, fixture manifest | None | Four-way totals reconcile; identical-payload comparison recorded |
| P1 | Shared interfaces and build targets | P0 | Existing Windows/Linux behavior and Windows adapters retained |
| P2 | libFLAC and SHA-256 | P1 | Old/new cross-restoration and PCM boundary cases pass |
| P3 | MOV reader/writer, ALAC default/PCM copy, private-atom specification | P0/P1 | Camera MOV round trip, full SHA-256, seek, accounting pass |
| P4 | AVI reader and independent MKV backend | P1/P2 | Historical archive restoration, seek, FFmpeg-free default linkage pass |
| P5 | GStreamer decoder/source and mapping proposal | P1; readers as needed | Playback, A/V sync, seek, EOS, errors pass without gst-libav |
| P6 | macOS CLI and private-runtime player | P2/P3; P5 for player | Clean arm64 Mac round trip and visible/audible playback pass |
| P7 | Finder thumbnail/preview routing | P3/P6 | Coexistence and cold-cache thumbnail/preview tested separately |
| P8 | MediaExtension proof of concept | P3/P6 | Host-specific routing, audio/video, seek, and distribution gates pass |

P9 is a follow-up AVI ALAC and optional FFmpeg backend proof of concept after P2/P3/P4. Gate it on installed/missing executable behavior, capability failures, recovery preservation and exact reconstruction; do not block the default release.

Prioritize MOV P3 before independent AVI/MKV P4. Codec-only P5 work can start after P1. P8 does not block the independent player release. Estimate schedules after P0/P1 identify input variants and implementation effort.

Test real camera MOV and old/new archives; exact JPEG, PCM, and whole-file restoration; PCM8/16/24/32 and supported channel counts; short FLAC blocks; 15/30/30000:1001 fps and 8040/44100/48000 Hz; seek/flush; 2/4 GiB boundaries and memory use; malformed tables, truncation, overflow, duplicate recovery data, and hashes. Add parser/recovery fuzzing. Check stride/color/orientation and audible clicks separately from byte equality. Verify clean-machine packaging and builds without the FFmpeg SDK. Experimental AVI ALAC, optional FFmpeg backend, GStreamer ALAC routing, upstream caps, shared-UTI routing, and native Apple host compatibility remain explicit open gates.

## Official references

Reviewed 2026-11-06. API availability does not establish MJXL interoperability.

- [QuickTime File Format](https://developer.apple.com/documentation/quicktime-file-format) and [user data atoms](https://developer.apple.com/documentation/quicktime-file-format/user_data_atom).
- [libFLAC encoder](https://xiph.org/flac/api/group__flac__encoder.html), [decoder](https://xiph.org/flac/api/group__flac__decoder.html), and [Matroska libraries](https://www.matroska.org/downloads/third_party.html).
- [GstVideoDecoder](https://gstreamer.freedesktop.org/documentation/video/gstvideodecoder.html), [qtdemux](https://gstreamer.freedesktop.org/documentation/isomp4/qtdemux.html), and [macOS private deployment](https://gstreamer.freedesktop.org/documentation/deploying/mac-osx.html).
- [Quick Look Thumbnailing](https://developer.apple.com/documentation/quicklookthumbnailing).
- [Custom formats and decoders](https://developer.apple.com/documentation/mediaextension/supporting-custom-media-formats-and-decoders) and [MEVideoDecoder](https://developer.apple.com/documentation/mediaextension/mevideodecoder).
