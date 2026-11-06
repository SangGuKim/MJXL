# 작업 기록

[English](WORKLOG.md) | **한국어**

## 2026-11-05: CMake 진입점과 안내 정리

현재 작업 경로는 `P:\tool\mjxl`이다. USB의 `W:\proj\mjxl` 대신 원격 소스에서 받은 체크아웃을 사용한다. 기존 구현과 검증 이력은 루트 `STATUS.md`를 참고한다. 사용자가 이전 팟플레이어 코덱 선택 문제가 캐시 삭제 후 해결됐음을 확인했다. 이를 미해결 코덱 결함으로 다시 취급하지 않는다.

### 문제

루트 `MJXL.sln`은 CMake가 만든 `_build/vs2026`의 프로젝트를 참조했다. 이 폴더는 Git에서 제외하면서 `.sln`만 저장소에 포함해, 새 체크아웃에서 모든 프로젝트가 누락됐다. CMake 진입점도 `native`에 있어 저장소 루트에서 빌드 방식을 파악하기 어려웠다. 영문·한국어 README의 네이티브 기능 설명도 서로 달랐다.

### 변경

- 루트 `CMakeLists.txt`에서 Windows 네이티브 프로젝트를 연결한다. Linux CLI는 기존 Makefile 경로를 유지한다.
- 생성 솔루션과 프로젝트를 Git에서 제외한다. 사용자가 재생성한 기존 로컬 루트 솔루션은 보존하고 추적만 해제한다. 새 스크립트는 빌드 폴더에만 솔루션을 생성한다.
- 기본 빌드 폴더를 `_build/windows-vs2026`으로 바꿔 기존 `native` 진입점의 캐시를 보존한다. 다른 소스 디렉터리의 캐시는 설명과 함께 거부하며 자동 삭제하지 않는다.
- `build.ps1 -ConfigureOnly`는 구성·솔루션 생성까지만 수행한다. 일반 실행은 전체 Release 빌드와 실행용 DLL 복사까지 수행하며 각 단계를 표시한다.
- 두 루트 README의 목차·기능·예제·설치·호환성 범위를 맞춘다. Python 연구 도구의 상세 설명은 언어별 `doc/experiments.*.md`로 옮긴다.

### 이어서 사용할 명령

저장소 루트에서 실행한다.

```powershell
.\native\prepare_deps.ps1
.\native\build.ps1 -ConfigureOnly
# 필요할 때 전체 빌드를 실행한다.
.\native\build.ps1
# 설치는 사용자가 직접 수행한다.
.\native\install.cmd
```

생성한 솔루션은 `_build/windows-vs2026/mjxl.sln`이다. `FFMPEG_SDK`를 우선하고 `FFMPEG_PATH`도 지원한다. 기존 빌드 산출물과 로컬 설치본은 이 문서 정리 작업으로 삭제하거나 재설치하지 않는다.

### 구성 검증 결과

루트 구성과 `-ConfigureOnly`가 성공했고 생성된 20개 프로젝트 경로가 모두 존재했다. 프로젝트 소스를 컴파일하지 않았으며 CMake 자체 컴파일러 검사는 별도이다. 기존 로컬 루트 솔루션이 변경되지 않았고 이전 `native` 기준 캐시는 수정 없이 재사용이 거부됐다. 새 진입점에서 작은 `mjxl_graph_test` 타깃 빌드가 성공했다. 두 README의 명령 예제·링크와 UTF-8/LF·Git 공백 검사를 통과했다. 전체 코덱 재빌드와 재설치는 하지 않았다.

## 2026-11-05: 공개 소스의 영어 기본 언어

사용자 요청으로 소스 주석·설명 문자열·스크립트 안내와 기본 문서를 영어로 통일한다. 이번 요청은 앞서 주어진 C/C++ 주석의 한국어 규칙보다 우선한다. 한국어 문서는 `.ko.md`로 보존한다. 외부 의존성, Git 내부 파일, 생성물과 개인 입력 자료는 번역 대상에서 제외한다.

언어 전환 링크를 포함한 기본 문서에 한국어가 남지 않게 하고 보고서 생성도 영어를 기본으로 한다. `.gitignore`를 포함한 저장소의 텍스트 파일 전체를 검사한다. 알고리즘과 측정 수치는 바꾸지 않는다.

숨김 설정을 포함한 저장소 텍스트 51개를 검사했으며 한국어는 의도한 `.ko.md` 7개에만 남았다. C++의 주석 외 내용은 동일하고 Python 변경도 문자열·주석에 한정됨을 확인했다. Python·PowerShell 문법, 문서 링크, 기존 수치와 영문 보고서 생성 검사를 통과했다. `-ConfigureOnly`가 영문 안내로 완료됐다. 변경 파일은 UTF-8 BOM 없음·LF를 유지했다. 번역을 위해 설치·인증서 작업이나 전체 코덱 재빌드는 실행하지 않았다.

## 2026-11-05: 주기적인 PCM 재생 잡음 보고

사용자가 팟플레이어에서 변환 AVI에 주기적인 틱 소리가 섞이고 원본 MOV는 정상이라고 보고했다. 파일은 `samples/IMG_20040402_034913.MOV`와 `.avi`이며 `encode -e 8`만 사용했으므로 기본 PCM 경로이다.

- 영상은 1,278프레임·15fps·85.2초이다.
- 양쪽 모두 unsigned PCM8·mono·8,040Hz이다.
- PCM은 685,008바이트이고 SHA-256은 양쪽 모두 `d8ceba93e29cbac7b62b83f61b3740e14d99236a57e05a6bceaeb8bdbb2c1b21`이다.
- 현재 CLI로 JPEG·PCM 일치와 원본 전체 복원을 검증했다.

PCM 일치만으로 플레이어 출력의 청감상 문제를 해결됐다고 판단하지 않는다. 재생 문제는 미해결이며 음성 알고리즘이나 작성기는 수정하지 않았다. `results/audio-diagnostics`에 다음 무손실 비교본을 만들었다.

- `IMG_20040402_034913-restored-mjpeg.avi`: 네이티브 작성기로 원래 JPEG·PCM을 복원한 AVI이다.
- `IMG_20040402_034913-ffmpeg-mjpeg.avi`: FFmpeg로 원본 스트림을 AVI에 다시 담았다.
- `IMG_20040402_034913-pcm.wav`: 동일한 PCM을 WAV에 담았다.

사용자에게 같은 팟플레이어 설정에서 세 파일을 비교하도록 요청했다. 청취 결과로 컨테이너·분리기 문제와 MJXL 재생 경로 문제를 구분해야 한다.

### 청취 결과와 AVI 헤더 비교본

사용자가 WAV는 정상이고 원래 MJXL AVI는 시작부터 끊기며 27초 부근에서도 몇 프레임마다 끊기는 듯한 소리가 난다고 확인했다. 네이티브로 복원한 MJPEG AVI에도 잡음이 있지만 FFmpeg AVI 두 종류는 정상이라고 했다. 저장 PCM 변경이나 JXL 고유 문제보다 네이티브 AVI 작성 경로를 조사할 근거이다.

네이티브 파일의 AVI 플래그는 인덱스만 표시한 `0x10`이며 청크를 교차 배치했는데도 `AVIF_ISINTERLEAVED`가 없다. PCM 권장 버퍼는 65,536바이트이고 실제 청크는 1,024·584바이트가 번갈아 나온다. 한 쌍은 0.2초, 즉 영상 세 프레임 분량이다. FFmpeg는 플래그 `0x910`과 1,024바이트 음성 버퍼를 쓴다. 차이는 확인했지만 원인으로 확정하지 않았다.

PCM 해시를 유지한 헤더 비교본 세 개를 만들었다. `-test-interleaved.avi`는 `0x100`만 추가하고, `-test-audio-buffer.avi`는 버퍼만 1,024로 바꾸며, `-test-both.avi`는 둘 다 적용한다. 사용자 청취를 기다린다. 원본과 작성기 소스는 수정하지 않았다.

첫 FFmpeg 비교본의 영상 패킷·시간 단위가 달라 추가로 `-ffmpeg-mjpeg-cfr.avi`와 `-ffmpeg-mjxl-cfr.avi`를 15fps·1,278프레임·85.2초로 맞췄다. 이 SDK는 JXL 패킷을 합쳐 버려 `-fflags +noparse+nofillin`으로 파서를 끄고 스트림 복사를 했다. CFR MJXL은 `--no-original`로 JPEG·PCM 일치를 검증했다. 진단용 리먹스 파일에는 원본 전체 복원 정보가 없다.

사용자가 작은 헤더 비교본 세 개와 네이티브 복원 MJPEG 모두 끊긴다고 확인했다. CFR FFmpeg 두 파일은 정상이다. 초기 비-CFR FFmpeg JXL은 화면이 멈추고 소리만 정상이므로 영상 재생 검증에 쓰지 않는다.

구간 이식 실험에서 헤더로 범위를 좁혔다. 정상 FFmpeg의 `hdrl`과 네이티브 청크·인덱스를 결합한 `-test-ffmpeg-header.avi`는 정상이고, 네이티브 헤더와 FFmpeg 청크·인덱스를 결합한 `-test-ffmpeg-layout.avi`는 끊긴다. 양쪽 모두 JPEG·PCM과 원본 전체 복원을 통과했다. 청크 배치만으로는 설명되지 않는다.

음성 스트림 헤더·리스트만 교체한 `-test-audio-header.avi`와 메인·영상 헤더를 교체한 `-test-main-video-header.avi`를 만들었다. 양쪽 모두 원본 복원과 PCM 해시를 통과했다. 음성 헤더의 차이는 handler 0/1, 권장 버퍼 65,536/1,024, PCM `strf` 길이 18/16이다. 버퍼만 바꾼 실험은 이미 실패했다.

### 원인과 수정

이후 청취 결과는 다음과 같다. 모든 파일의 PCM 해시는 같다.

| 비교본 | 네이티브 AVI에서 바꾼 점 | 결과 |
| --- | --- | --- |
| `-test-main-video-header.avi` | FFmpeg 메인·영상 헤더 | 끊김 |
| `-test-audio-header.avi` | FFmpeg 음성 헤더·리스트 | 정상 |
| `-test-only-handler.avi` | 음성 `fccHandler` 0에서 1로 | 끊김 |
| `-test-only-junk.avi` | 음성 `strl`에 4,120바이트 `JUNK` 추가 | 끊김 |
| `-test-only-strf16.avi` | `strf` 18에서 16바이트로, `movi`는 오프셋 312로 이동 | 정상 |
| `-test-strf18-aligned.avi` | 18바이트 `strf`, `movi` 4,440(4바이트 정렬) | 끊김 |
| `-test-strf16-unaligned.avi` | 16바이트 `strf`, `movi` 4,442(비정렬) | 정상 |

마지막 두 비교본으로 `strf` 변경과 함께 바뀌었던 `movi` 정렬을 원인에서 제외했다. 원인은 `cbSize`가 0인 18바이트 PCM `strf`이다. 팟플레이어가 왜 이 형식에 반응하는지는 내부를 확인하지 않아 알 수 없다.

`AviWriter`는 이제 PCM에 16바이트 `PCMWAVEFORMAT`을 쓴다. FLAC 헤더는 확장 형식이 필요하므로 바꾸지 않았다. `encode -e 8`로 새로 만든 `-fixed-pcm.avi`가 정상임을 사용자가 확인했다.

현재 `samples/`에 있는 샘플 4개가 PCM·FLAC 인코딩, 검증, 원본 SHA-256 검사를 통과했다. PCM AVI는 DirectShow 재생·탐색, 로컬 Media Foundation 테스트, FFmpeg 음성 디코딩도 통과했다(`results/windows-strf16-validation`). 청취는 8비트 mono PCM인 `IMG_20040402_034913`만 대상으로 했다. 이전에 만든 AVI는 다시 인코딩하기 전까지 기존 헤더를 유지한다.

## 2026-11-05: FLAC을 기본 음성 저장 방식으로 변경

사용자가 FLAC 재생에 문제가 없다고 보고하고, FLAC을 기본으로 하고 원본 음성을 유지하는 별도 스위치를 두도록 요청했다. `encode`는 이제 음성을 기본적으로 FLAC으로 저장한다. `--copy-audio`는 음성을 그대로 복사하며 `--flac`을 대체한다. `--flac`은 이제 알 수 없는 옵션으로 거부한다. `--pcm`은 입력 형식을 가리키는 이름이라 쓰지 않고 동작을 나타내는 이름을 택했다. 입력 음성은 여전히 PCM이거나 MJXL 메타데이터가 있는 FLAC이어야 하며 다른 코덱은 두 모드 모두 거부한다. 원래 32비트 PCM은 계속 PCM으로 둔다.

샘플 4개가 두 모드 모두 인코딩, 검증, 원본 SHA-256 검사를 통과했다. 두 모드의 AVI는 DirectShow 영상 재생·탐색과 FFmpeg 음성 디코딩도 통과했다(`results/windows-copy-audio-validation`). 기본 출력의 음성은 `flac`, `--copy-audio` 출력의 음성은 `pcm_u8`이다. 압축 강도 7에서 `IMG_20040402_034913`의 크기는 FLAC 22,320,444바이트, 복사 22,592,870바이트이다.

FLAC AVI에 관한 기존 기록은 그대로 유효하다. Windows에는 이를 푸는 기본 DirectShow 디코더가 없고 Media Foundation AVI 소스는 이를 읽을 때 멈춘다. 이 경로에는 `--copy-audio`를 사용한다. 새 기본값으로 탐색기 썸네일을 새로 생성하는 검사는 다시 하지 않았다.

### 후속 점검과 다음 작업 참고 사항

- 로컬 HEAD와 원격 `main`이 `6af9d708879d183a96b079aabfffe6e3021b1085`로 일치하며, 이 메모 작성 전 작업 폴더가 깨끗함을 확인했다. 의도적으로 바꾼 FLAC 기본값을 되돌리거나 예제에 `--flac`을 다시 쓰지 않는다. 과거 기록은 당시 동작을 설명한다.
- 현재 CLI로 `results/windows-copy-audio-validation`의 출력 8개(원본 4개, PCM·FLAC)에 `verify`를 독립적으로 다시 실행했다. JPEG·PCM 일치와 원본 파일 전체 검증을 통과했고, 원본 SHA-256과 출력 크기도 보고서와 일치했다. 이번 점검에서 인코딩과 청취를 반복하지는 않았다.
- 추적 중인 텍스트 파일 51개를 점검했다. 한국어는 `.ko.md`에만 있고 로컬 Markdown 링크는 유효하다. 푸시한 커밋은 Git 공백 검사를 통과했다.
- 이후 AVI 작성기 회귀 검사는 새 출력 폴더를 사용한다. `validate_native.py`는 AVI가 이미 있으면 재인코딩 대신 검증만 하므로, 검증 통과만으로 현재 작성기가 만든 헤더임을 확인할 수 없다.
- 원본 복원 정보는 배치 표와 미디어 이외의 모든 바이트를 각각 Brotli 최대 품질로 압축해 `mjxo`에 저장한다. MOV 헤더뿐 아니라 컨테이너 메타데이터와 패딩도 포함한다. JPEG·PCM은 중복 저장하지 않고 미디어 스트림에서 복원하며 원본 SHA-256으로 결과를 검증한다.

## 2026-11-05: 구성 요소별 통계와 프로젝트 동기 설명

인코딩 마지막에 음성·영상·컨테이너/복원 정보·전체 파일의 원본 바이트 수, 저장 바이트 수, 절감률을 표로 출력한다. 스트림 행은 패킷 데이터만 계산하고 나머지 행은 컨테이너 비용과 복원 정보를 모두 포함해 합계가 실제 파일 크기와 일치하게 한다. 절감률이 음수이면 크기가 늘어난 것이며, 원본이 0바이트인 항목은 `n/a`로 표시한다.

루트 영어·한국어 README는 MJXL을 Motion JPEG XL로 소개하고 JPEG XL의 JPEG 가역 재압축과 원본 파일 전체 보존의 연결을 설명한다. 보존용 파일에서는 작은 절감률도 의미가 있음을 밝히고, 손실 H.265 감상본 및 픽셀만의 무손실 압축과 목적을 구분한다. GStreamer 연동과 macOS 재생은 현재 지원이 아닌 향후 계획으로 명시한다.

Windows CLI를 빌드하고 실제 63프레임 MOV의 기본 FLAC·`--copy-audio` 출력과 영상만 있는 AVI를 검사했다. 모든 바이트 수와 절감률을 독립적인 FFprobe 입력 패킷 합계 및 출력 RIFF 청크 합계와 대조했다. 출력 3개 모두 원본 파일 전체의 정확한 복원 검증도 통과했다. 검사 결과는 `C:/Temp/mjxl-stats-validation`에 있다. 검사에는 압축 강도 1을 사용했으며 압축 성능 측정값으로 사용하지 않는다. 설치본은 갱신하지 않았고 커밋·푸시도 수행하지 않았다.

## 2026-11-05: FFmpeg를 이용한 실험용 MKV 출력

`encode`는 최종 출력 확장자로 AVI 또는 Matroska를 선택하고 대소문자는 구분하지 않는다. 다른 출력 확장자는 AVI로 위장해 쓰는 대신 거부한다. MKV는 기존 FFmpeg Matroska 작성기의 `V_MS/VFW/FOURCC`와 `MJXL`을 이용하며 추가 라이브러리는 필요 없다. 원본 복원 정보는 MIME 형식이 `application/x-mjxl-original`인 `mjxl-original.bin` 첨부 데이터에 저장한다. FLAC의 원래 PCM 비트 깊이는 음성 트랙의 `MJXL_ORIGINAL_BITS` 태그에 남긴다. AVI는 기존 `mjxo`·`mjxa` 표현을 유지한다. `verify`·`restore-original`·`bench`는 MKV를 읽는다. `restore`는 MJPEG AVI만 쓰며 출력 확장자도 검사한다.

읽기 경로는 복원용 첨부 데이터를 허용하고 관련 없는 추가 스트림은 거부한다. 원본 전체 복원은 중복 첨부 데이터와 복원 정보 누락을 거부한다. `--no-original`의 검증에는 AVI와 같은 명시적 허용이 필요하다. 검증 스크립트는 `--container avi|mkv`를 받으며 기본은 AVI이다. 보고서는 컨테이너 중립적인 `encoded_bytes`와 `container` 필드를 사용한다.

Windows 검증 결과는 다음과 같다.

- `results/matroska-v1`: 실제 MOV 4개를 PCM 복사·기본 FLAC으로 각각 변환했다. 변환, JPEG·PCM 검증, 원본 파일의 실제 복원과 SHA-256 비교, FFmpeg 전체 영상·음성 디코딩을 통과했다. MJPEG AVI 복원과 RGB 디코딩도 통과했다.
- `results/matroska-edge-v1`: 합성한 무음·16/24/32비트 스테레오 입력에서 두 음성 모드의 정확한 원본 복원, MJPEG AVI 복원, FFmpeg 디코딩을 통과했다. PCM32는 PCM으로 유지됐다. 대문자 `.MKV`, 복원 정보 생략 시 거부/명시적 허용, 미지원 확장자 거부, 덮어쓰기 거부, 기존 AVI 읽기, 새 AVI 출력도 검사했다.
- 사용자가 4.2초 MKV 두 개의 팟플레이어 영상·음성 재생과 탐색이 정상임을 확인했다. 사용자가 정정한 미지원 항목은 Preview 창이 아니라 탐색기 **썸네일**이다. Preview는 검사하지 않았다. 썸네일 처리기·레지스트리 등록·코덱 설치·썸네일 캐시는 변경하지 않았다.
- 같은 압축 강도와 같은 미디어 데이터에서도 샘플 4개의 MKV는 AVI보다 970~21,638바이트 컸다. `IMG_20040402_034913`의 강도 7 결과는 PCM 복사 AVI 22,592,870바이트 대 MKV 22,605,136바이트, FLAC AVI 22,320,444바이트 대 MKV 22,340,768바이트이다. MKV의 컨테이너 비용이 더 작다고 가정하지 않는다.

CLI는 `dist/Release`에 재빌드했으며 로컬 설치본은 갱신하지 않았다. MKV 경로의 Linux 컴파일, 대용량 MKV, DirectShow/MFT MKV 그래프 검사는 수행하지 않았다. 기존 그래프 검사는 AVI Splitter를 명시적으로 사용하므로 MKV 검증으로 인용하지 않는다. 향후 썸네일 지원은 전용 MJXL 셸 처리기 또는 JPEG 커버 첨부와 Icaros 같은 처리기를 이용하는 경로가 있으며 이번에는 구현하지 않았다. 커밋·푸시는 수행하지 않았다.

## 2026-11-05: MOV 저장 가능성 시험과 작업 종료 인계

사용자가 MOV에도 MJXL을 넣을 수 있는지 물었다. 설치된 FFmpeg SDK로 `MJXL` 태그의 영상과 PCM8 음성을 MOV에 담는 시험에 성공했다. 검증한 파일은 `C:/Temp/mjxl-mov-pcm-check.mov`이며 크기는 2,575,893바이트이다. 입력은 `results/windows-copy-audio-validation/pcm/IMG_20040318_230535.avi`이고 파서를 비활성화했다. `mjxl verify samples/IMG_20040318_230535.MOV C:/Temp/mjxl-mov-pcm-check.mov --no-original`에서 JPEG 63프레임과 PCM 33,768바이트가 정확히 일치했다. FFmpeg 전체 영상·음성 디코딩도 통과했다. 영상은 15fps·시간 단위 1/15360이며 음성은 1/8040이다. 이 MOV의 팟플레이어 재생과 탐색기 썸네일은 검사하지 않았다.

재현 명령은 다음과 같다. SDK와 저장소 경로는 실행 환경에 맞게 조정한다.

```powershell
& "$env:FFMPEG_PATH/bin/ffmpeg.exe" -v error -n -fflags +noparse+nofillin -c:v libjxl -i results/windows-copy-audio-validation/pcm/IMG_20040318_230535.avi -map 0:v:0 -map 0:a:0 -c copy -tag:v MJXL -f mov C:/Temp/mjxl-mov-pcm-check.mov
```

이 파일은 저장 가능성을 확인한 시험본이며 원본 파일 복원 정보가 없다. 원본 전체 보존용으로 취급하거나 미디어 일치 검사만으로 원본을 지우지 않는다. 먼저 MKV 비교본에서 만든 `C:/Temp/mjxl-mov-feasibility.mov`는 밀리초로 반올림된 타임스탬프를 유지해 CLI의 고정 프레임 레이트 검사를 실패했다. AVI에서 만든 검증 성공 파일과 혼동하지 않는다. FLAC MOV 시험본 `C:/Temp/mjxl-mov-flac-feasibility.mov`는 `flac only supported in MP4` 오류로 헤더 작성에 실패했으므로 사용할 수 없다. 시험 파일은 삭제하지 않았다.

CLI의 MOV 출력은 **구현하지 않았다**. `.mov`는 여전히 거부한다. 향후 구현에는 PCM을 유지하는 MOV 작성, 복원 정보를 저장할 전용 atom 정의와 읽기·검증·복원 연결, 재생·썸네일 검사가 필요하다. 저장과 강제 디코더 지정 FFmpeg 검사의 성공만으로 QuickTime·macOS 재생 지원을 추정하지 않는다. 이번 검사로 MP4 지원을 확인한 것도 아니다.

사용자 요청으로 오늘 작업은 여기서 종료한다. 현재 기준은 원격·로컬 `main` 커밋 `6af9d70`과 추적 파일 10개의 미커밋 변경이다. 루트 README 두 개, native README 두 개, 작업 로그 두 개, `native/encoder.cpp`, `native/cli.cpp`, `native/original_file.hpp`, `native/validate_cli.py`에 구성 요소별 통계·프로젝트 동기와 계획·실험용 MKV 지원을 반영했다. `dist/Release`의 CLI는 재빌드했고 설치본은 갱신하지 않았다. 설치·커밋·푸시·정리 삭제는 수행하지 않았다. 재개할 때 이 변경과 무시된 샘플·결과·빌드 파일을 보존한다. 사용자가 요청하면 MOV 지원 또는 전용 탐색기 썸네일 경로를 이어갈 수 있으나 이번 중단 뒤 자동으로 진행하지 않는다.

### 후속 요청: 소스 체크포인트 저장

사용자가 다른 PC에서 이어갈 수 있도록 오늘 소스·문서의 커밋과 푸시를 추가 요청했다. 앞의 미커밋·미푸시 상태는 이 요청 이전 기록이다. 이 체크포인트에는 소스와 문서만 포함하며 샘플·시험 영상·의존성·빌드 결과는 로컬에만 있고 Git에서 제외한다. 다른 PC에서는 `main`을 가져온 뒤 의존성을 준비하고 재빌드한 다음 설치한다. `C:/Temp`의 MOV 시험본이나 무시된 `results` 파일이 그 PC에도 있다고 가정하지 않는다. 오늘 추가 구현은 진행하지 않는다.

## 2026-11-06: 이식성 첫 구현

음성·영상·복원 payload·순수 컨테이너의 네 항목 통계와 총합 검사, native/testdata의 fixture SHA-256 목록을 추가했다. 원본 복원 payload는 version 1을 유지한다. SHA-256은 FFmpeg 대신 출처를 고정한 PicoSHA2(MIT), FLAC 인코딩·디코딩은 libFLAC 1.5.0을 사용한다. 최종 STREAMINFO와 기존 PCM8 표현을 유지하고 callback 예외를 C ABI 밖으로 전파하지 않는다. 손상 CRC와 잘린 패킷은 거부하며 원본 전체 SHA-256 검증도 유지한다.

컨테이너 구현을 container.cpp로 옮기고 FFmpeg 타입을 노출하지 않는 container.hpp를 추가했다. CMake에 코어·오디오·아카이브·컨테이너 타깃, 선택적 Windows 어댑터와 공통 테스트, Linux/macOS system-package 경로를 구성했다. Linux/macOS 경로는 해당 플랫폼에서 아직 검증하지 않았다. CLI 컨테이너 backend는 여전히 FFmpeg가 필요하며 MOV/ALAC·GStreamer·자체 컨테이너 대체·macOS 앱과 확장은 미구현이다.

--audio-codec auto|flac|alac|copy와 기존 --copy-audio 별칭을 연결했다. AVI/MKV auto는 지원 PCM에 FLAC을 사용하고 PCM32는 복사한다. 명시적 FLAC 미지원 형식, 상충 옵션과 ALAC backend 미구현 요청은 오류로 처리한다. AVI ALAC 실험 스위치와 외부 FFmpeg backend는 아직 노출하지 않는다.

Windows x64 / VS2026 v145 검증 결과는 다음과 같다.

- 기존 DirectShow/MFT를 포함한 Release 빌드에 성공했다. 재생·등록 재검사는 아니며 설치와 등록은 수행하지 않았다. 실행 파일은 dist/Release에 있다.
- CLI와 Windows 어댑터를 끈 별도 구성의 CTest에 성공했다. PCM8/16/24, 1/2/8채널, 8040/44100/48000 Hz, 1/5003샘플의 54개 FLAC 조합과 손상·잘림 거부, 정책·CLI 충돌, SHA-256 known answer, 아카이브 무결성을 검사했다. 테스트 실행 파일의 import는 KERNEL32.dll뿐이며 FFmpeg DLL에 의존하지 않는다.
- 실제 카메라 MOV 7개를 AVI/MKV와 PCM/FLAC의 28개 조합으로 새로 인코딩하고 미디어 검증, 물리 원본 복원과 SHA-256 비교, 네 항목 총합 검사를 통과했다. 컨테이너별 MJPEG 복원과 첫 샘플 RGB bench도 통과했다. 회귀 검사에는 effort 1을 사용했으므로 압축률 benchmark가 아니다. 결과는 results/portability-v1-avi와 results/portability-v1-mkv에 있다.
- 과거 FFmpeg FLAC AVI 7개도 원본 MOV와 전체 복원 검증을 통과했다.
- 새 출력 28개는 독립 FFmpeg 디코딩에서 원본과 정규화 PCM이 일치했고 영상 디코딩도 통과했다. AVI/MKV의 영상·음성 payload 14쌍은 각각 동일했다. 미구현·잘못된·상충 codec 옵션은 출력 파일 생성 없이 거부했다. 요약은 results/portability-crosscheck.json에 있다.

libFLAC 버전은 1507800de4b70e21be71f38caa0d9079d0bc6e45로 고정하고 FLAC·PicoSHA2 라이선스를 배포 폴더에 복사한다. libFLAC 1.5.0의 win_utf8_io.c에 빠진 할당 함수 선언은 MSVC 타깃의 stdlib.h 강제 include로 보완하며 다운로드한 소스는 변경하지 않는다. 커밋과 push는 수행하지 않았다.
