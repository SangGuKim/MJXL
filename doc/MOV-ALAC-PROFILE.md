# MJXL MOV and ALAC profile, version 1

[한국어](MOV-ALAC-PROFILE.ko.md)

Status: implemented experimental profile, 2026-11-06. Identifiers below are private project conventions, not registered codec/container standards. The CLI still uses FFmpeg to read media containers and write MKV. ALAC itself uses Apple's portable source without FFmpeg; AVI and MOV writing are native.

## Container layout

MOV is self-contained and nonfragmented: qt-compatible ftyp, an extended-size mdat with sequential media, then moov. Sample tables use stsd, run-compressed stts, stsc, stsz and co64. Every video sample is independently decodable, so no partial sync table is written. Video track timescale is fps numerator and each video sample duration is its denominator. Audio time uses the integer sample rate. Version 1 movie/track/media headers carry 64-bit durations. Fast-start relocation, fragmented MOV, external data references and edit timelines are outside this writer profile. Large-file offsets are represented but large-file memory behavior is not validated; input packets and recovery data remain buffered.

Video sample entry MJXL contains one complete JPEG XL frame per sample and uses the existing reversible JPEG reconstruction path. This does not establish native QuickTime decoding of MJXL video. Existing source orientation/color metadata handling has not been expanded by this change.

Audio is ALAC by default, or PCM with --copy-audio / --audio-codec copy. ALAC uses a 24-byte big-endian ALACSpecificConfig inside a full alac atom (36 bytes including header/version/flags). Sound description version 0 is used up to 65535 Hz and version 2 above that rate. PCM uses version 2 lpcm, interleaved little-endian integers, signed except unsigned PCM8. Known multichannel PCM speaker masks are recorded in a chan bitmap atom.

Compressed audio uses one packet per container sample, normally 4096 PCM frames, with the actual duration for the final partial packet. PCM chunks group multiple sample frames rather than adding one table entry per byte.

## Private recovery metadata

Under moov/udta:

| Atom | Payload |
| --- | --- |
| MJXO | Unmodified pack_original version 1 payload, including original layout, compressed residue and SHA-256 |
| MJXA | Big-endian uint32 metadata version (1), then uint32 original PCM depth (8/16/24/32); present for ALAC |

Atom sizes include their headers. Duplicate requested metadata, invalid sizes, truncation and more than eight recursive metadata levels are rejected. PCM depth is obtained from its sample description; ALAC original depth must not be inferred from the stored depth. External remuxers may discard private metadata. Missing MJXO means the file cannot restore its complete original; the existing --no-original verification policy applies.

MKV ALAC uses A_ALAC through the transitional FFmpeg muxer. CodecPrivate contains the 36-byte alac configuration atom. MJXL_ORIGINAL_BITS records original depth and mjxl-original.bin retains the existing recovery attachment convention.

## PCM and speaker order

ALAC supports the current 8/16/24/32-bit integer profile at 1–384000 Hz and up to eight channels, subject to supported speaker layouts. PCM8 is promoted to signed PCM16 using `(u8 - 128) * 256`, preserving playback amplitude. Decoding requires the low byte to be zero before reconstructing PCM8. PCM16/24/32 retain their integer precision with no resampling.

The adapter reorders canonical speaker-ordered PCM into ALAC element order and reverses that permutation on decode. Supported known masks correspond to mono, stereo, 3.0, 4.0, 5.0 back, 5.1 back, 6.1 back and 7.1 wide back. A known incompatible layout, such as quad or ordinary 7.1, is not silently relabeled: an explicit ALAC request fails and MOV auto selects PCM. A zero mask denotes the codec's default layout for that channel count. This restriction is separate from FLAC's existing profile.

Apple ALAC is pinned to c38887c5c5e64a4b31108733bd79ca9b2496d987, with its Apache license distributed. Generated source copies add bit-buffer bounds, partial-frame limits, remaining-input entropy bounds and a zero-shift predictor fix; downloaded sources remain untouched. The adapter supports little-endian hosts and bounded 32–16384 frame configurations. These checks are not a claim of comprehensive fuzz validation.

## Experimental AVI ALAC

Enable only with `--audio-codec alac --experimental-avi-alac`. The private WAVE format tag is 0xA1AC; it is not a registered interoperability mapping. strf contains WAVEFORMATEX followed by the 36-byte alac configuration atom. Existing variable-size audio chunk timing is reused, and mjxa retains original depth.

Native verification/restoration works with this mapping. External FFmpeg automatic audio identification does not; forcing input decoder `-c:a alac` is required in the checked SDK. No Windows AVI Splitter or PotPlayer support is claimed. AVI auto therefore remains FLAC (PCM copy for unsupported FLAC formats), and MKV auto also remains FLAC. Installing FFmpeg does not change these choices.

## Validation

Use [validate_audio_containers.py](../native/validate_audio_containers.py) for exact original restoration, normalized external PCM comparison, external video decoding and cross-container packet digest comparisons. Compression measurements and platform limitations are recorded separately in the audio comparison report. QuickTime Player, Finder, macOS, Linux and GStreamer playback require their own platform tests.

References: [Apple ALAC](https://macosforge.github.io/alac/), [ALAC configuration description](https://github.com/macosforge/alac/blob/master/ALACMagicCookieDescription.txt), [Matroska A_ALAC mapping](https://www.matroska.org/technical/codec_specs.html), [QuickTime file format](https://developer.apple.com/documentation/quicktime-file-format).
