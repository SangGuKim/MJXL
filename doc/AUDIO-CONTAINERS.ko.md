# ALAC·FLAC 및 컨테이너 비교

[English](AUDIO-CONTAINERS.md)

2026-11-06 Windows x64 / VS2026 v145에서 측정한 결과이다. libjxl 0.11.2, libFLAC 1.5.0 compression level 8, Apple ALAC c38887c5 기본 인코더·4096프레임 packet을 사용했다. JXL effort는 회귀 검사 시간을 줄이기 위해 1로 고정했다. 오디오 압축률은 JXL effort와 별개이며 이 결과를 모든 입력이나 다른 ALAC 구현에 일반화하지 않는다.

## 실제 카메라 샘플

7개 입력의 각 코덱별 영상·음성 packet payload가 컨테이너 사이에서 동일함을 확인했다. 아래 값은 MKV 오디오 packet 합계이며 컨테이너 header·codec configuration·복원 payload는 포함하지 않는다.

| 입력 | PCM 바이트 | FLAC 바이트 | ALAC 바이트 | FLAC 대비 ALAC 증가 |
| --- | ---: | ---: | ---: | ---: |
| IMG_20040318_225819.MOV | 15,008 | 11,605 | 26,795 | 130.89% |
| IMG_20040320_230212.MOV | 89,512 | 59,994 | 149,755 | 149.62% |
| IMG_20040321_000945.MOV | 42,344 | 30,579 | 73,382 | 139.98% |
| IMG_20040322_025026.MOV | 97,016 | 79,668 | 177,866 | 123.26% |
| IMG_20040325_190603.MOV | 70,752 | 50,996 | 123,535 | 142.24% |
| IMG_20040326_055048.MOV | 85,760 | 60,880 | 147,755 | 142.70% |
| IMG_20060430_203434.MOV | 156,000 | 80,527 | 240,239 | 198.33% |
| 합계 | 556,392 | 374,249 | 939,327 | 150.99% |

이 PCM8 대상에서 FLAC은 원본 오디오보다 32.74% 작았고 ALAC은 68.82% 컸다. PCM8을 재생 진폭을 유지하는 PCM16으로 가역 승격한 ALAC 결과이다. 첫 입력의 FFmpeg ALAC 오디오 payload는 26,793바이트로 Apple 구현의 26,795바이트와 2바이트 차이였다.

| 저장 경로 | 전체 파일 합계 | 순수 컨테이너 합계 |
| --- | ---: | ---: |
| AVI / FLAC | 54,374,462 | 42,118 |
| MKV / FLAC | 54,399,041 | 66,697 |
| MKV / ALAC | 54,962,913 | 65,491 |
| MOV / COPY | 54,550,271 | 35,784 |
| MOV / ALAC | 54,922,942 | 25,520 |

같은 MKV 기준 ALAC 전체 파일은 FLAC보다 563,872바이트, 1.04% 컸다. 전체 파일에는 원본 복원 payload가 포함된다. 컨테이너 크기만으로 ALAC/FLAC 오디오 차이를 상쇄하지 못했다. 따라서 MOV는 Apple 호환성을 목표로 ALAC 기본값을 사용하되, 이 PCM8 자료에서 작은 MOV를 원하면 --copy-audio를 선택한다. AVI/MKV는 FLAC 기본값을 유지한다.

## 합성 입력

0.4초의 결정적 다중 사인 신호이며 실제 녹음의 대표 benchmark가 아니다. 30000/1001 fps MJPEG와 함께 사용했다.

| 입력 | PCM 바이트 | FLAC 바이트 | ALAC 바이트 |
| --- | ---: | ---: | ---: |
| pcm16-stereo.mov | 70,560 | 13,385 | 15,306 |
| pcm16-surround.mov | 230,400 | 44,216 | 52,360 |
| pcm24-stereo.mov | 115,200 | 23,792 | 54,664 |
| pcm32-stereo.mov | 153,600 | — | 93,051 |

PCM32는 현재 FLAC 프로필에서 명시적으로 거부하며 auto는 PCM으로 둔다. ALAC은 PCM32도 정확히 복원했다. 5.1채널은 내부 복원뿐 아니라 FFmpeg의 정규화 PCM과 비교해 채널 순서도 확인했다.

## 호환성 결과

| 경로 | 자체 도구의 원본 복원 | FFmpeg 자동 오디오 인식 | 비고 |
| --- | --- | --- | --- |
| MOV / ALAC·PCM | 통과 | 통과 | 자체 MOV writer이며 QuickTime Player 실기기 검증은 별도이다. |
| MKV / ALAC·FLAC·PCM | 통과 | 통과 | MKV writer는 아직 FFmpeg이다. |
| AVI / FLAC·PCM | 통과 | 통과 | 기존 기본 경로를 유지한다. |
| AVI / 실험 ALAC | 통과 | 실패 | -c:a alac을 명시하면 PCM 디코딩이 일치한다. 기본값으로 승격하지 않는다. |

실제 파일 56개와 합성 파일 30개 조합에서 정확한 원본 복원·외부 PCM 비교·컨테이너 간 media digest 비교를 수행했다. PCM32 FLAC의 명시적 거부 2건은 성공 인코딩 수에서 제외한다. AVI 실험은 --audio-codec alac --experimental-avi-alac을 요구한다. 팟플레이어·Windows AVI Splitter·GStreamer·macOS·QuickTime Player·Finder는 이번 검증에 포함하지 않았다. 파일 형식은 [MOV·ALAC 프로필](MOV-ALAC-PROFILE.ko.md)에 정의한다.


## 재현

```powershell
python -X utf8 native/validate_audio_containers.py --binary dist/Release/mjxl.exe --ffmpeg C:/sdk/ffmpeg/bin/ffmpeg.exe --ffprobe C:/sdk/ffmpeg/bin/ffprobe.exe --samples samples --output results/new-audio-comparison --effort 1
```

sgtk Python 환경과 새 출력 폴더를 사용한다. 입력·출력 미디어는 Git에서 제외하는 로컬 산출물이며 JSON 보고서는 지정한 출력 폴더에 남긴다. 입력의 정확한 hash도 기록한다.

추가 경계 검증에서 96 kHz ALAC, quad의 MOV auto PCM 전환과 명시적 ALAC 거부, 영상 전용 3컨테이너, seek, --no-original의 검증·복원 거부 정책, 잘못된 atom 크기와 중복 MJXO 거부를 확인했다. Linux/macOS 실행과 대용량 파일은 이번 결과에 포함하지 않는다.


## 후속 재생 확인

팟플레이어에서 MOV의 ALAC은 재생되지만 MJXL 영상 디코더가 연결되지 않는 것을 확인했다. FFmpeg 재포장 MOV+PCM도 같았다. ffplay에 -vcodec libjxl을 지정한 MOV+ALAC은 사용자가 실제 재생을 확인했으며 MFT는 필요하지 않다. 사용자는 MJXL MOV의 탐색기 썸네일도 표시되지 않는다고 보고했다. 두 경로의 공통 원인은 미확인이다. 위 표의 FFmpeg 검사 성공을 팟플레이어·썸네일 지원으로 해석하지 않는다.
