# MJXL 이식성과 컨테이너 설계 및 구현 계획

[English](PORTABILITY-PLAN.md) | 한국어

작성일: 2026-11-06. 상태: 단계별 구현 중인 설계안이다. 이 문서는 FFmpeg 필수 의존성 제거, MOV 입출력, GStreamer 재생, macOS 지원을 하나의 로드맵으로 정의한다. 아래의 새 API·타깃·옵션 이름은 제안이며 현재 제공 기능이 아니다.


## 2026-11-06 구현 상태

첫 구현에서는 크기 통계, 계층 분리의 기반, libFLAC/SHA-256 교체를 반영한다. CLI 컨테이너 backend는 아직 FFmpeg를 필수로 사용한다. MOV/ALAC, 자체 AVI/MOV reader, 독립 MKV backend, GStreamer, macOS 앱·확장은 아직 구현하지 않았다.

- P0: 네 항목 통계와 총합 검증, [fixture 해시 목록](../native/testdata/portability-fixtures.json)을 추가했다. 같은 입력·effort의 AVI/MKV 미디어 payload를 비교한다.
- P1: mjxl_core, mjxl_audio, mjxl_archive, mjxl_container로 경계를 나누고 공통 헤더의 FFmpeg 타입 노출을 제거했다. reader는 아직 buffered Movie 계약이다. PacketRef의 시간 정보, streaming/spool과 전체 Reader 인터페이스는 남아 있다.
- P2: libFLAC 1.5.0 callback backend와 vendored PicoSHA2를 사용한다. 기존 복원 payload와 PCM8 표현을 유지한다. Windows 검증을 수행하며 Linux/macOS 실행 검증은 남아 있다.
- 오디오 선택: --audio-codec auto|flac|alac|copy를 파싱하고 기존 --copy-audio를 유지한다. ALAC은 backend 미구현 오류로 거부하며 출력은 만들지 않는다. 명시적 FLAC의 미지원 PCM 형식과 상충 옵션도 오류로 처리한다. AVI/MKV auto는 FLAC 또는 지원 형식 밖의 PCM copy를 고른다.
- CMake: 공통 타깃은 FFmpeg 없이 구성·검사할 수 있다. Linux/macOS system-library 경로를 추가했으나 해당 플랫폼 검증 완료로 간주하지 않는다.


## 1. 목표와 불변 조건

- JPEG 비트스트림, PCM 바이트, 원본 파일 전체의 세 수준을 구분하고 각각 검증한다. 화면이 같다는 사실을 원본 보존의 근거로 사용하지 않는다.
- CLI와 보존 코어는 FFmpeg와 GStreamer 없이 빌드·실행한다. GStreamer는 선택적 재생 어댑터이다. FFmpeg는 독립 검증 도구 및 명시적으로 선택하는 선택적 실행 backend로 활용한다.
- 기존 AVI의 mjxo/mjxa, MKV의 복원 attachment와 MJXL_ORIGINAL_BITS, 기존 FLAC의 PCM8 변환을 계속 읽는다. 새 컨테이너 때문에 복원 payload 버전을 바꾸지 않는다.
- 한 영상·최대 한 오디오, 독립 JXL 프레임, CFR을 초기 지원 범위로 유지한다. 임의 MOV, DRM, 외부 미디어 참조, fragmented MP4, VFR과 편집 타임라인은 초기 범위에서 제외한다. 지원하지 않는 파일은 쓰기 전에 거부한다.
- 기존 파일 덮어쓰기 금지, partial 출력과 검증 후 이름 확정, --no-original의 명시적 선택을 유지한다.

## 2. 현재 상태와 실제 문제

native/core.cpp의 libjxl 코어와 DirectShow/MFT 디코더는 이미 FFmpeg와 분리되어 있다. native/encoder.cpp가 demux, AVI 쓰기, FFmpeg 기반 MKV 쓰기, 검증과 CLI 실행을 함께 담당한다. native/flac_audio.cpp는 libavcodec, native/original_file.cpp는 libavutil SHA-256에 의존한다. 루트 CMake는 Windows 전용이며 Linux는 별도 Makefile을 사용한다.

현재 AVI writer는 2 GiB 미만의 classic AVI만 쓴다. AVI 리더는 FFmpeg에 의존한다. 입력 패킷과 전체 파일을 메모리에 보유하므로 64비트 offset만 추가해도 대용량 처리가 완성되지는 않는다.

기존 기록에서 같은 미디어 payload의 MKV는 AVI보다 970~21,638바이트 컸다. 이것은 해당 샘플의 결과이며 컨테이너 일반의 우열이 아니다. MOV 시험은 MJXL+PCM의 JPEG/PCM 일치를 확인했지만 복원 데이터를 포함하지 않았다. FLAC MOV는 당시 FFmpeg muxer가 거부했다. 해당 실패를 MOV 전체의 불가능성으로 해석하지 않는다. 자세한 근거는 [작업 기록](WORKLOG.md)에 있다.

## 3. 공통 아키텍처

의존 방향은 플랫폼 어댑터 → 미디어/컨테이너 계층 → 코덱 및 바이트 입출력 계층으로 한다. 공통 헤더에 AV*, Gst*, Windows, Apple 타입을 노출하지 않는다.

| 제안 타깃 | 책임과 의존성 |
| --- | --- |
| mjxl_codec | JPEG↔JXL, RGB 출력. libjxl과 thread runner를 사용한다. |
| mjxl_audio | PCM 표현과 FLAC/ALAC 변환. libFLAC과 Apple ALAC 구현을 사용한다. |
| mjxl_archive | 원본 layout/residue, SHA-256, Brotli payload를 처리한다. |
| mjxl_container | 공통 reader/writer 인터페이스와 AVI/MOV/MKV backend를 제공한다. MKV만 libmatroska/libebml에 의존한다. |
| mjxl CLI | 변환·검증·복원·통계 작업을 조합한다. |
| gst-mjxl | 선택적 source/demux 어댑터와 디코더를 제공한다. |
| Windows / macOS 어댑터 | 기존 DirectShow/MFT, 신규 macOS 앱·확장을 연결한다. |

컨테이너 계층은 Rational, TrackInfo, PacketRef, Reader, Writer로 구성한다. PacketRef에는 트랙 ID, 정수 PTS/DTS/duration과 time base, 원본 file offset·length, keyframe 정보를 둔다. 원본 복원용 물리 바이트 범위와 재생용 패킷 경계를 구분한다. PCM 패킷 재분할이 원본 layout을 변경하지 않도록 한다.

Reader는 헤더 조사, 트랙 열거, 순차 읽기, 시간 기반 seek, 복원 payload 읽기를 제공한다. Writer는 트랙 정의, 패킷 쓰기, 복원 payload 기록, finish를 제공한다. 알 수 없는 추가 트랙을 조용히 버리지 않는다. 컨테이너의 알 수 없는 메타데이터는 재생 시 안전하게 건너뛰며 보존 경로에서는 원본 residue로 보존한다.

초기에는 기존 메모리 모델을 인터페이스 뒤로 옮겨 동작을 보존한다. 이후 seek 가능한 ByteSource와 임시 spool로 미디어·복원 residue를 스트리밍하고 64비트 checked arithmetic을 사용한다. 큰 표도 spool하고 명시적 메모리 예산을 둔다. 상수 크기 메모리를 검증하기 전 대용량 지원을 선언하지 않는다.

## 4. FFmpeg 제거

libFLAC callback으로 기존 FlacStream/FlacDecoder 계약을 대체한다. PCM8은 기존처럼 16비트 상위 바이트로 보존하고 PCM16/24도 기존 파일과 상호 복원한다. PCM32는 초기에는 그대로 저장한다. STREAMINFO의 최종 sample count·MD5, 마지막 짧은 프레임, 채널 순서와 샘플 단위 timestamp를 보존한다. FFmpeg compression_level 12와 libFLAC 설정은 일대일 대응으로 간주하지 않으며 크기·속도를 다시 측정한다.

SHA-256은 소형의 검토 가능한 이식형 구현을 vendor하고 출처·라이선스를 기록한다. 표준 known-answer와 FFmpeg 기반 기존 payload를 비교한다. 새 암호 라이브러리 전체를 이 용도만으로 필수화하지 않는다.

AVI는 RIFF/LIST 경계, word padding, strh/strf, movi/rec list, idx1을 읽는다. 초기에는 기존 classic AVI writer 호환을 완료한다. 외부 AVI와 OpenDML/AVIX, indx/ix##는 별도 단계로 지원하거나 명시적으로 거부한다. MOV와 달리 큰 AVI는 이 단계가 끝날 때까지 제한을 유지한다.

MKV는 libmatroska/libebml을 사용하되 기존 V_MS/VFW/FOURCC+MJXL, attachment 이름/MIME, 오디오 메타데이터를 유지한다. timestamp, cluster, cues, attachment를 처리하는 glue는 별도 구현한다. 패키지별 버전·라이선스·정적/동적 링크 조건을 확정한다.

전환 중에만 선택적 FFmpeg backend를 비교용으로 유지한다. 최종 기본 빌드는 FFmpeg SDK 탐색·링크·DLL 복사를 제거하고, 이전 backend는 개발 옵션으로 격리한 뒤 호환성 검증 후 삭제한다.

## 5. MOV 설계

### 파일 구조와 지원 프로필

비분할 self-contained MOV를 자체 구현한다. mdat에 패킷을 연속 기록하고 마지막에 moov를 작성해 선행 전체 인덱스 계산을 피한다. extended-size atom과 co64를 지원한다. fast-start를 위한 moov 선행 배치는 후속 작업으로 둔다.

moov/trak/mdia/minf/stbl에 stsd, stts, stsc, stsz, stco 또는 co64를 쓴다. 독립 영상은 모두 sync sample이며 불필요한 프레임별 stss를 생략할 수 있다. CFR 영상은 stts를 run으로 표현한다. fps=num/den이면 표현 가능 범위에서 track timescale=num, sample duration=den을 사용한다. movie timescale과 각 track timescale의 변환은 오버플로를 검사한 정수 연산으로 수행한다. MKV를 경유한 밀리초 반올림을 MOV 원본 시간으로 사용하지 않는다.

초기 영상 sample entry는 실험용 MJXL을 유지하고 한 sample에 완전한 독립 JXL payload 하나를 넣는다. 이것은 표준화된 JPEG XL 동영상 매핑이라는 주장이 아니다. 크기·색상·pixel aspect ratio·orientation을 보존하거나 적용하는 정책을 명시하고 어댑터들이 같은 정책을 따른다.

PCM을 먼저 지원한다. 입력 MOV에서 실제 관찰한 PCM sample entry, endian, signedness, sample rate와 채널 표현을 목록화하고 프로필을 확정한다. container sample과 CLI PCM packet을 동일 개념으로 간주하지 않는다. 오디오 청크는 샘플 경계로 묶고 sample table을 바이트마다 만들지 않는다.

MOV는 ALAC을 기본으로 사용한다. 컨테이너별 선택 정책과 선택적 FFmpeg 경로는 다음 절에 정의한다.


### 컨테이너별 오디오 정책과 선택적 FFmpeg

FFmpeg 의존성은 줄이되 설치되어 있으면 명시적으로 활용할 수 있게 한다. FFmpeg 없는 기본 빌드·실행·원본 복원이 완결되어야 하며, FFmpeg를 독립 검증 도구로만 제한하지 않는다. 선택적 실행 backend의 구체적인 구현은 후속 단계로 둔다.

| 컨테이너 | auto 기본 정책 | 명시적 대안 |
| --- | --- | --- |
| MOV | ALAC | PCM copy |
| MKV | 우선 FLAC, ALAC 호환성 검증 후 기본값 통일 여부 결정 | ALAC 또는 PCM copy |
| AVI | FLAC, 해당 PCM 형식이 FLAC 대상이 아니면 PCM copy | PCM copy; ALAC은 후속 실험 스위치 |

정책은 한 곳의 capability 표로 관리한다. 제안 옵션은 --audio-codec auto|alac|flac|copy와 --backend native|ffmpeg이며 이름은 구현 시 확정한다. --copy-audio는 기존 호환 별칭으로 유지하고 상충 옵션은 오류로 처리한다. auto는 컨테이너와 PCM 형식의 지원 여부로 선택하며 선택된 코덱·backend와 선택 이유를 출력한다. 명시한 코덱이 실패하면 조용히 다른 코덱으로 바꾸지 않는다. auto에서도 파일 손상이나 인코딩 오류를 포맷 미지원으로 취급해 fallback하지 않는다.

MOV ALAC은 Apple 공개 코덱 구현을 공통 인코더·디코더로 사용하고 라이선스를 함께 배포한다. PCM8은 기존과 같이 16비트 상위 바이트로 가역 변환하고 원래 depth를 기록한다. PCM16/24/32와 채널 순서, 마지막 짧은 packet 및 codec configuration을 검증한다. libFLAC은 기존 파일 읽기와 AVI/MKV 기본 경로에 유지한다. GStreamer의 ALAC 디코더 경로가 gst-libav를 요구하는지는 별도로 조사하며, 필요하면 공통 ALAC 코덱 어댑터를 추가해 기본 재생 경로의 FFmpeg 비의존 조건을 지킨다.

AVI+ALAC은 후속 명시적 실험 옵션(제안: --experimental-avi-alac)으로만 허용한다. format tag/configuration, AVI Splitter, PotPlayer, 자체 reader, GStreamer, seek와 정확 복원을 검증한다. 성공하면 MOV/MKV/AVI의 ALAC 기본값 통일을 검토하고, 실패하면 AVI는 FLAC 또는 PCM copy를 유지한다. 파일을 쓸 수 있다는 사실만으로 호환성을 인정하지 않는다.

선택적 FFmpeg backend는 SDK 필수 링크보다 별도 ffmpeg 실행 파일을 사용하는 방식부터 검토한다. 명시적 경로 지정과 실행 파일 탐색, 버전·encoder/muxer capability 확인을 제공한다. 설치 여부만으로 기본 backend나 재현 가능한 코덱 선택을 바꾸지 않는다. 사용자가 FFmpeg backend와 코덱을 선택하면 native capability 표에서 제외한 컨테이너 조합도 시도할 수 있다. 단, FFmpeg muxer 자체의 제약을 우회할 수 있다고 약속하지 않으며 지원하지 않는 조합은 오류로 반환한다.

FFmpeg가 없거나 요청 encoder/muxer가 없으면 명확한 오류를 반환한다. subprocess는 shell 문자열 대신 인수 배열로 실행하고 취소·종료 코드·stderr·partial 출력을 처리한다. 실험적 강제 조합의 옵션 이름은 후속 PoC에서 확정한다. 비손실 코덱만 보존 모드에서 허용하며 lossy 변환은 별도 파생본 기능으로 분리한다. private 복원 atom/attachment가 누락되는 단순 remux는 완전한 보존 출력으로 인정하지 않는다. 코덱 패킷과 구성 정보를 공통 writer에 전달하거나 컨테이너별 복원 데이터를 다시 삽입하고, 최종 파일에서 JPEG/PCM/전체 SHA-256 검증까지 통과해야 한다. 일반 FFmpeg transcode에서 JPEG를 재인코딩하면 원본 보존이 깨지므로 영상 payload는 그대로 유지한다.

ALAC 구현 참고: [Apple ALAC source and license](https://macosforge.github.io/alac/) 및 [supported PCM formats](https://github.com/macosforge/alac/blob/master/ReadMe.txt).

### 원본 복원 데이터

moov/udta 아래 전용 MJXO atom을 두는 안을 채택한다. payload는 기존 version 1의 pack_original 결과를 그대로 저장한다. 오디오 원본 depth 등 보조 정보는 버전이 있는 MJXA atom으로 분리한다. atom 이름·배치·byte order·중복 거부·길이 제한을 구현 전 형식 명세로 고정한다. 현재 AVI의 소문자 chunk 이름을 MOV에 그대로 등록된 것처럼 사용하지 않는다.

복원 payload는 영상/오디오 sample table에 넣지 않는다. 검증은 저장된 MOV를 다시 읽어 JPEG와 PCM을 복원하고 source SHA-256과 비교한 뒤 완료한다. 외부 remuxer가 private atom을 버릴 수 있으므로 미디어 재생 성공과 보존 성공을 구별한다. --no-original 동작도 기존과 동일하게 한다.

### reader와 크기 비교

reader는 nested atom 경계, 32/64비트 크기, stsd/stts/stsc/stsz/stco/co64를 검사한다. 자기 출력과 대상 카메라 MOV부터 시작한다. 비정상 edit list, 외부 data reference, 압축 moov, fragmented 파일과 지원하지 않는 트랙은 구체적인 사유로 거부한다. 물리 sample 범위를 복원 계층에 전달한다.

순수 컨테이너와 복원 데이터를 분리해 다음 항등식을 출력한다.

`전체 파일 = 영상 payload + 오디오 payload + 복원 payload + 순수 컨테이너`

원본은 복원 payload가 0이다. atom/chunk/attachment wrapper는 순수 컨테이너에 포함한다. 같은 JXL·오디오·복원 payload를 AVI/MKV/MOV에 넣어 비교하고 그 바이트 동일성도 확인한다. 원본 MOV 대 미디어 전용 MOV 대 복원 포함 MOV를 따로 보고한다. AVI는 패킷당 8바이트 청크 헤더와 16바이트 idx1 및 padding을 사용하므로 MOV 청크 묶음이 유리할 수 있으나 실측 전 절감량을 약속하지 않는다.

## 6. GStreamer 지원

### decoder와 demux는 별도 문제이다

mjxldec를 GstVideoDecoder로 구현한다. 실험용 sink caps는 video/x-mjxl, parsed=true, alignment=frame과 width/height/framerate를 제안한다. set_format에서 형식을 검증하고 handle_frame에서 libjxl RGB 출력을 얻어 협상된 RGB/RGBA buffer에 stride를 고려해 복사한다. gst_video_decoder_finish_frame으로 timestamp와 duration을 이어 준다. flush/seek/EOS, 중단, 오류 전파와 worker 수 상한을 처리한다. 인스턴스별 runner를 사용하고 동시 썸네일/재생의 과도한 스레드 생성을 막는다.

avidemux/qtdemux/matroskademux가 임의 MJXL 코드를 알아서 위 caps로 내보낸다고 가정하지 않는다. 각 버전의 실제 caps를 먼저 수집하고 FOURCC/sample entry/Matroska mapping 패치를 제안한다. 기존 demux가 거부한 뒤 capsfilter만 붙여 해결할 수는 없다.

upstream mapping이 없을 때의 배포 경로는 공통 reader를 재사용하는 mjxlsrc이다. 로컬 파일용 URI source bin으로 등록하고 영상·오디오 패킷 pad와 시간 query/seek를 제공한다. 일반 MOV 전체를 선점하지 않도록 기본 generic MOV URI 처리 우선순위를 높이지 않는다. 초기 앱은 해당 source를 명시적으로 선택한다. 필요하면 이후 pull-mode demux 어댑터로 확장한다.

초기 재생은 mjxlsrc → queue → mjxldec → videoconvert → 영상 sink, 오디오는 별도 queue → PCM 처리 또는 flacparse/flacdec → audioconvert → audioresample → 오디오 sink로 구성한다. 이것은 목표 구성도이며 현재 실행 가능한 명령이 아니다. A/V 동기화는 공통 clock과 segment를 사용한다. sample 단위 timestamp를 nanosecond로 바꿀 때 누적 덧셈 대신 원점 기준 rescale을 사용한다.

첫 배포 목표는 명시적 pipeline 재생이다. 그다음 playbin/decodebin 자동 선택, seek, pause/resume을 통과시킨다. gst-libav 설치 없이 검증해 FFmpeg 간접 의존이 생기지 않도록 한다. encoding/muxing과 복원 attachment를 보존하는 GStreamer 변환은 후속 범위이다. 재생 플러그인이 설치되어도 VLC/IINA가 자동 지원되는 것은 아니다.

## 7. macOS 지원

### A. CLI와 독립 재생 앱

공통 CMake를 Windows/Linux/macOS에 적용하고 Windows SDK·DirectShow·MFT와 MSVC 옵션을 WIN32/MSVC 조건 아래 둔다. Apple Clang의 C++20 라이브러리 지원을 확인하고 std::format 등 필요한 대체를 결정한다. 초기 검증 기준은 Apple Silicon arm64와 macOS 15 이상으로 제안한다. 최소 OS는 실제 빌드·실행으로 확정한다. Intel/universal2는 두 아키텍처의 모든 라이브러리와 실기기/CI 검증이 준비된 뒤 제공한다.

CLI의 encode/verify/restore-original을 먼저 완성한다. 이후 GStreamer를 앱 내부에 private 배포한 간단한 플레이어를 만든다. 앱 번들의 runtime, 플러그인, plugin scanner, 라이브러리 경로를 고정하고 시스템 GStreamer 설치 없이 실행한다. 배포 시 라이선스, 재서명 순서, hardened runtime, notarization과 quarantine 상태에서의 실행을 검증한다. GStreamer 설치를 QuickTime 코덱 등록으로 설명하지 않는다.

### B. Finder 썸네일과 Quick Look

호스트 앱에 QLThumbnailProvider 기반 Thumbnail Extension을 추가한다. 전체 파일 복원 대신 공통 reader로 첫 번째 또는 대표 JXL frame 하나만 찾아 RGB를 만들고 크기에 맞춰 그린다. 파일 접근 권한, 취소, 시간·메모리 상한, malformed file 실패를 처리한다. Quick Look Preview Extension은 별도 타깃이며 처음에는 정적 이미지와 메타데이터를 보여준다. 동영상 재생 preview는 후속 단계이다.

일반 .mov/.avi/.mkv는 공유 UTI이므로 MJXL codec에만 확장을 선택시키는 것이 가능하다고 전제하지 않는다. 기존 파일 형식을 빼앗지 않는 실제 routing을 실험한다. 필요하면 명시적으로 선택하는 전용 확장자/UTType을 후속 제안하되 기존 파일을 자동 변경하지 않는다. 이 게이트가 실패하면 초기에는 앱 내 썸네일만 제공한다. thumbnail과 Space 키 preview의 성공 여부는 따로 기록한다.

### C. Apple 네이티브 미디어 연동

macOS 15 이상 MediaExtension의 video decoder를 장기 경로로 둔다. 먼저 MJXL MOV sample entry가 내장 MOV reader를 통과해 decoder extension까지 도달하는지 검증한다. 성공하면 libjxl 출력을 MEVideoDecoderPixelBufferManager가 할당한 CVPixelBuffer에 stride와 색상 정보에 맞춰 기록한다. ObjC++ bridge 또는 좁은 C API로 C++ 예외·객체 수명을 경계 안에 가둔다.

decoder만으로 sample routing이 안 되거나 AVI/MKV를 처리하려면 공통 reader를 이용하는 Format Reader extension을 별도 검토한다. 오디오 지원과 시간·seek 정보도 이 경로에서 검증한다. entitlement, 서명, 사용자 활성화와 호스트 앱별 정책을 확인한다. QuickTime Player, AVFoundation 테스트 앱, Finder, 지원되는 전문 앱을 각각 검사하며 한 앱의 성공을 OS 전체 지원으로 표현하지 않는다. 하드웨어 JXL decoding은 전제하지 않는다.

## 8. 단계별 구현과 완료 기준

| 단계 | 산출물 | 선행 조건 | 완료 기준 |
| --- | --- | --- | --- |
| P0 측정·명세 | 4분류 통계, format profile, 기준 fixture manifest | 없음 | 기존 결과와 총합 일치, 동일 media/recovery payload 비교표 확보 |
| P1 계층 분리 | 공통 타입/reader/writer, CMake 타깃 분리 | P0 | 기존 Windows/Linux 명령과 DirectShow/MFT 빌드 회귀 없음 |
| P2 FLAC·hash | libFLAC backend, 독립 SHA-256 | P1 | 기존/신규 파일 교차 복원, PCM8/16/24·채널·마지막 블록 검증 |
| P3 MOV | 자체 MOV reader/writer, ALAC 기본·PCM copy, MJXO/MJXA 명세 | P1, P0 | 실제 카메라 MOV→MJXL MOV→원본 SHA-256 일치, seek·통계 검증 |
| P4 AVI·MKV | 자체 AVI reader, 독립 MKV backend | P1, P2 | 기존 AVI/MKV fixture 복원과 seek, FFmpeg 없는 기본 링크 확인 |
| P5 GStreamer | mjxldec, 명시적 mjxlsrc, caps mapping 제안 | P1; 각 reader 사용 시 P3/P4 | gst-libav 없이 재생·A/V sync·seek·EOS·오류 처리 통과 |
| P6 macOS 기본 | CLI 빌드, private GStreamer 앱 배포 | P2/P3; 앱은 P5 | 깨끗한 arm64 Mac에서 CLI round trip과 실제 시청·청취 통과 |
| P7 Finder | thumbnail/preview 확장, UTType routing 실험 | P3, P6 | 공유 확장자 비침해, cold-cache 썸네일·preview 각각 확인 |
| P8 네이티브 | MediaExtension decoder PoC, 필요 시 reader | P3, P6 | 대상 앱별 sample 전달·decode·audio·seek·배포 게이트 통과 |

P9는 AVI+ALAC 실험과 선택적 FFmpeg 실행 backend의 후속 PoC로 둔다. 설치 유무별 동작, capability 거부, 복원 데이터 유지와 정확 복원을 완료 기준으로 삼는다. P2/P3/P4 이후에 진행하며 기본 배포를 막지 않는다.

P3은 P4보다 먼저 진행해 MOV 비교와 macOS 기반을 빨리 확보한다. P5의 codec 단위 개발은 P1 뒤 시작할 수 있다. P8 결과를 P6 배포의 필수 조건으로 묶지 않는다. 일정은 P0/P1에서 입력 변형과 작업량을 확인한 뒤 추정한다.

## 9. 검증 행렬과 출시 게이트

- 보존: 실제 카메라 MOV, 기존 AVI/MKV, 새 MOV를 대상으로 원본 JPEG·PCM·전체 파일 SHA-256을 각각 비교한다. FFmpeg는 별도 교차 검증에만 사용한다.
- 오디오: 무음 트랙 없음, PCM8/16/24/32, mono/stereo와 지원 최대 채널, 짧은 마지막 FLAC block을 포함한다. PCM32는 FLAC 미사용을 확인한다. 실제 청취로 클릭과 seek 직후 잡음도 검사한다.
- 시간: 15/30 및 30000/1001 fps, 오디오 8040/44100/48000 Hz, 비정수 A/V 길이, 시작 offset 거부, 처음/중간/끝 seek와 반복 flush를 검사한다.
- 크기: 동일 effort/library 버전과 payload를 고정한다. 복원 포함/미포함을 혼합 비교하지 않는다. 2 GiB/4 GiB 경계와 co64는 생성 fixture로 검사하고 전체 메모리 상한도 측정한다.
- 파서: truncation, overflow, 범위 중첩, 큰 선언 크기, 중복 복원 atom/attachment, 손상 hash와 지원하지 않는 sample table을 거부한다. container parser와 recovery decoder에 fuzz 입력을 추가한다.
- 표시: stride, RGB/BGRA, 색상, orientation, aspect ratio를 Windows/Linux/macOS에서 비교한다. 썸네일·preview·재생·정확 복원은 별도 결과이다.
- 배포: Windows 기존 설치 경로, Linux 깨끗한 환경, macOS 서명·notarization된 앱을 검사한다. FFmpeg 개발 SDK가 없어도 기본 configure/build와 실행이 성공해야 한다.

AVI+ALAC 실험, 선택적 FFmpeg backend, GStreamer ALAC 경로, GStreamer upstream mapping, shared-UTI Finder routing, MediaExtension 호스트 호환성은 미해결 게이트이다. 통과하기 전 README의 현재 지원 표에 완료로 표시하지 않는다. 구현 PR마다 이 문서의 해당 상태와 실제 검증 환경을 갱신한다.

## 10. 근거 자료

아래 공식 문서를 2026-11-06 기준으로 검토했다. API 지원과 실제 MJXL 호환성은 별개이다.

- [Apple QuickTime File Format](https://developer.apple.com/documentation/quicktime-file-format) 및 [User data atom](https://developer.apple.com/documentation/quicktime-file-format/user_data_atom): MOV 구조와 private metadata 설계 근거이다.
- [libFLAC encoder](https://xiph.org/flac/api/group__flac__encoder.html) 및 [decoder](https://xiph.org/flac/api/group__flac__decoder.html): callback 기반 코덱 교체 근거이다.
- [Matroska libraries](https://www.matroska.org/downloads/third_party.html): libmatroska/libebml backend 후보의 근거이다.
- [GstVideoDecoder](https://gstreamer.freedesktop.org/documentation/video/gstvideodecoder.html), [qtdemux](https://gstreamer.freedesktop.org/documentation/isomp4/qtdemux.html), [macOS deployment](https://gstreamer.freedesktop.org/documentation/deploying/mac-osx.html): decoder 계약, demux 분리와 private 배포 근거이다.
- [Quick Look Thumbnailing](https://developer.apple.com/documentation/quicklookthumbnailing): 앱에 포함하는 Thumbnail Extension 경로의 근거이다.
- [Supporting custom media formats and decoders](https://developer.apple.com/documentation/mediaextension/supporting-custom-media-formats-and-decoders), [MEVideoDecoder](https://developer.apple.com/documentation/mediaextension/mevideodecoder): macOS 15 이상 샘플, entitlement 및 pixel buffer 연동의 근거이다.
