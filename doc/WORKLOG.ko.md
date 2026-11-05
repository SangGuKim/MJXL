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
