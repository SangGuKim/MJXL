# MJXL MOV·ALAC 프로필 version 1

[English](MOV-ALAC-PROFILE.md)

2026-11-06 구현한 실험 프로필이다. 아래 private 식별자는 프로젝트 규칙이며 등록된 표준이라는 뜻이 아니다. CLI의 컨테이너 읽기와 MKV 쓰기는 아직 FFmpeg를 사용한다. ALAC 코덱은 Apple 공개 구현을 사용하고 AVI·MOV 쓰기는 자체 구현이다.

## 컨테이너와 시간

MOV는 self-contained 비분할 파일이다. qt 호환 ftyp, 확장 크기 mdat, 후행 moov를 기록한다. stsd·stts·stsc·stsz·co64를 사용하며 영상은 모두 독립 sample이다. 영상 timescale은 fps 분자, sample duration은 분모이다. 오디오 timescale은 샘플률이다. movie·track·media header는 version 1의 64비트 duration을 사용한다. fast-start, fragmented 파일, 외부 데이터 참조와 편집 타임라인은 writer 범위 밖이다. 64비트 offset 표현과 별개로 입력 패킷·복원 데이터는 아직 메모리에 보유하며 대용량 처리 검증은 남아 있다.

MJXL video sample entry는 완전한 JXL 프레임 하나를 담는다. 원본 JPEG 바이트의 가역 복원 경로를 유지한다. 이것만으로 QuickTime의 MJXL 영상 재생이 되는 것은 아니며 이번 작업에서 원본 orientation·색상 메타데이터 처리를 확장하지 않았다.

MOV 오디오는 기본 ALAC이며 --copy-audio 또는 --audio-codec copy로 PCM을 선택한다. ALAC 구성은 big-endian 24바이트 ALACSpecificConfig를 full alac atom에 넣은 36바이트이다. 65535 Hz까지 sound description version 0, 그보다 높으면 version 2를 쓴다. PCM은 version 2 lpcm으로 저장하며 little-endian 정수·interleaved 형식이다. PCM8만 unsigned이고 나머지는 signed이다. 다채널 PCM의 알려진 speaker mask는 chan bitmap으로 기록한다.

ALAC container sample 하나는 보통 PCM 4096프레임에 대응하며 마지막 짧은 packet은 실제 길이를 기록한다. PCM은 여러 sample을 chunk로 묶는다.

## 복원 메타데이터

moov/udta 내부 규칙은 다음과 같다.

| Atom | Payload |
| --- | --- |
| MJXO | 원본 layout·Brotli residue·SHA-256을 담은 기존 pack_original version 1 payload |
| MJXA | big-endian uint32 version=1 및 uint32 원본 PCM depth=8/16/24/32. ALAC일 때 기록한다. |

중복 메타데이터·잘못된 크기·잘림과 8단계를 넘는 재귀를 거부한다. PCM depth는 sample description에서 얻고 ALAC의 원본 depth는 저장 depth에서 추측하지 않는다. 외부 remuxer는 private atom을 버릴 수 있다. MJXO가 없으면 원본 전체 복원이 불가능하며 기존 --no-original 검증 정책을 적용한다.

MKV는 기존 FFmpeg muxer를 통해 A_ALAC을 기록한다. CodecPrivate는 36바이트 alac 구성 atom이다. 원본 depth는 MJXL_ORIGINAL_BITS에, 복원 payload는 기존 mjxl-original.bin attachment에 저장한다.

## PCM과 채널 순서

현재 ALAC 프로필은 정수 PCM8/16/24/32, 1~384000 Hz, 최대 8채널을 지원하되 알려진 채널 배치 제한을 적용한다. PCM8은 `(u8 - 128) * 256`으로 signed PCM16에 넣어 재생 진폭을 유지한다. 복원 시 하위 바이트가 0인지 검사한다. PCM16/24/32는 정밀도를 유지하고 resampling하지 않는다.

공통 PCM speaker 순서를 ALAC 요소 순서로 바꾸고 디코딩 시 역변환한다. 알려진 배치는 mono·stereo·3.0·4.0·5.0 back·5.1 back·6.1 back·7.1 wide back을 지원한다. quad나 일반 7.1처럼 다른 배치를 임의로 재지정하지 않는다. 명시적 ALAC 요청은 거부하고 MOV auto는 PCM으로 전환한다. mask=0은 채널 수에 대응하는 codec 기본 배치를 뜻한다.

Apple 소스는 c38887c5c5e64a4b31108733bd79ca9b2496d987로 고정하고 Apache 라이선스를 배포한다. 다운로드 원본 대신 생성한 복사본에 비트 경계·부분 프레임 상한·entropy 잔여 길이 검사와 predictor의 0비트 shift 보완을 적용한다. 어댑터는 little-endian host 및 32~16384프레임 구성을 지원한다. 이를 포괄적인 fuzz 검증 완료로 간주하지 않는다.

## AVI ALAC 실험

--audio-codec alac --experimental-avi-alac을 함께 지정해야 한다. private WAVE format tag는 0xA1AC이며 등록된 호환 매핑이 아니다. strf는 WAVEFORMATEX와 36바이트 alac 구성 atom을 담는다. 기존 가변 크기 오디오 청크 시간 표현과 mjxa 원본 depth를 유지한다.

자체 도구의 검증·복원은 가능하다. 검사한 FFmpeg SDK는 자동 오디오 식별에 실패하며 입력 decoder에 -c:a alac을 명시해야 한다. Windows AVI Splitter·팟플레이어 호환성을 주장하지 않는다. 따라서 AVI/MKV auto는 FLAC을 유지하고 FLAC 미지원 형식은 PCM으로 둔다. FFmpeg 설치 유무로 이 선택을 바꾸지 않는다.

## 검증

[컨테이너 비교 스크립트](../native/validate_audio_containers.py)는 원본 전체 복원, 외부 정규화 PCM 비교, 영상 디코딩과 컨테이너 간 packet digest를 검사한다. 압축률과 플랫폼 한계는 별도 오디오 비교 결과에 기록한다. QuickTime Player·Finder·macOS·Linux·GStreamer 재생은 별도 플랫폼 검증이 필요하다.

공식 근거는 [Apple ALAC](https://macosforge.github.io/alac/), [ALAC 구성 명세](https://github.com/macosforge/alac/blob/master/ALACMagicCookieDescription.txt), [Matroska A_ALAC](https://www.matroska.org/technical/codec_specs.html), [QuickTime 형식](https://developer.apple.com/documentation/quicktime-file-format)이다.
