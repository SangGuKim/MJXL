# MJXL — Motion JPEG XL

[English](README.md) | **한국어**

기존 MJPEG 영상의 JPEG 프레임을 JPEG XL로 가역 재압축해 저장 공간을 줄이는 Windows·Linux 실험 프로젝트이다. 네이티브 CLI는 독립 JXL 프레임을 실험용 FourCC `MJXL`의 AVI·Matroska(MKV)·MOV에 저장한다. 세 컨테이너 모두 기본적으로 원본 파일 전체를 바이트 단위로 복원하는 정보도 포함한다. AVI/MKV 음성은 기본적으로 무손실 FLAC, MOV는 ALAC으로 저장하며 `--copy-audio`를 주면 그대로 복사한다.

## 왜 MJPEG를 이런 방식으로 보존하는가?

오래된 카메라 영상에 많이 쓰인 Motion JPEG는 각 영상 프레임이 JPEG 이미지인 형식이다. JPEG XL은 지원하는 JPEG 비트스트림을 가역 재압축하고 원래 JPEG 바이트를 정확하게 복원할 수 있다. **Motion JPEG XL은 이 기능을 MJPEG 영상의 모든 프레임에 적용한다.** 기존 JPEG 압축과 그 아티팩트를 그대로 보존하면서 추가적인 세대 손실 없이 크기를 줄인다.

작은 감상용 파일이 목적이면 손실 H.265 인코딩이 더 적합할 수 있다. 이 프로젝트의 목적은 원본을 복원할 수 있는 보존용 사본이다. 손실 재인코딩은 비슷하게 보이더라도 원래 JPEG 비트스트림을 재현하지 못한다. 디코딩한 픽셀을 무손실로 압축하는 것만으로도 원래 JPEG 파일이나 MOV 컨테이너가 보존되는 것은 아니다.

약 16%의 절감에도 의미가 있는 이유이다. 원본의 일부를 희생해 훨씬 작은 파일을 만드는 대신, 원본 복원 가능성을 유지하며 공간을 확보한다. 절감률은 입력에 따라 달라지며 보장하지 않는다. MJXL은 JPEG→JXL 가역 재압축, 음성 무손실 저장, Brotli로 압축한 컨테이너 복원 정보를 결합한다. 따라서 `restore-original`로 소스 파일 전체를 바이트 단위로 복원하고 SHA-256을 확인할 수 있다. 원본 미디어 데이터 자체를 한 벌 더 저장하지 않는다.

MJXL은 실험용 프로젝트 이름과 FourCC이며 표준화된 영상 코덱은 아니다. 아티팩트 보정은 이미지를 바꾸므로 별도의 감상용 파생본에서 다룰 일이며, 가역 보존 경로에는 포함하지 않는다.

**Windows 빌드는 CMake 프로젝트이다.** 루트의 `CMakeLists.txt`에서 시작한다. Visual Studio 솔루션과 프로젝트는 로컬에서 생성하며 Git에 넣지 않는다. Linux CLI는 `native/Makefile`로 빌드한다.

## Windows: 구성·빌드·설치

VS2026의 v145 C++ 도구 집합과 Windows SDK, CMake(VS 동봉 버전 지원), Git, `sgtk` conda 환경의 Python이 필요하다. 헤더·import library·DLL이 들어 있는 Windows FFmpeg SDK를 `FFMPEG_SDK`로 지정한다. 이전 `FFMPEG_PATH`도 지원한다. libjxl 0.11.2와 의존성은 별도로 준비하며 C++ 런타임과 함께 정적으로 링크한다.

저장소 루트에서 실행한다.

```powershell
.\native\prepare_deps.ps1
# 구성과 솔루션 생성만 수행한다. 컴파일하지 않는다.
.\native\build.ps1 -ConfigureOnly
# Release 전체 빌드 후 실행용 DLL과 라이선스를 복사한다.
.\native\build.ps1
# 빌드한 디코더와 도구를 현재 사용자에게 설치한다.
.\native\install.cmd
```

구성 후 `_build/windows-vs2026/mjxl.sln`을 VS2026에서 열거나, 루트 폴더를 CMake 프로젝트로 연다. CMake가 생성한 `.slnx`도 빌드 폴더에 있다. 첫 전체 빌드는 libjxl과 의존성까지 컴파일하므로 솔루션 생성은 앞 단계에 불과하다. 바로 전체 빌드를 원하면 `-ConfigureOnly`를 생략한다. 스크립트는 구성·솔루션 생성·컴파일·실행 파일 복사를 구분해 표시한다.

기존 `_build/vs2026` 캐시는 `native`를 소스 디렉터리로 사용하므로 새 루트 진입점과 함께 재사용할 수 없다. 새 기본 폴더를 사용해 기존 캐시를 보존한다. 다른 로컬 빌드 폴더는 `-BuildDirectory _build/windows-local`로 지정한다. 이전 로컬 루트 `MJXL.sln`이 남아 있어도 스크립트는 사용하거나 갱신하지 않는다.

빌드 결과는 `dist/Release`에 생긴다. `install.cmd`는 이 파일들을 `%LOCALAPPDATA%\MJXL`에 복사하고, 현재 사용자용 64비트 DirectShow 디코더를 등록하며 오래된 ActiveMovie `Filter Cache64`를 지운다. 빌드는 수행하지 않는다. 설치 후 플레이어를 다시 연다. `-DecoderOnly`는 디코더만 설치하고 `-Uninstall`은 설치 기록에 있는 파일만 제거한다. 재빌드 후 로컬 설치본을 바꾸려면 다시 설치한다. 파일 연결은 변경하지 않는다.

## 변환·검증·복원

입력 영상은 직접 준비하고 새 출력 경로를 사용한다. 기존 파일은 덮어쓰지 않는다.

```powershell
New-Item -ItemType Directory -Path results -Force
$mjxl = '.\dist\Release\mjxl.exe'
& $mjxl encode input.mov results\output.avi
# 출력 확장자로 Matroska 컨테이너를 선택한다.
& $mjxl encode input.mov results\output.mkv
& $mjxl verify input.mov results\output.avi
& $mjxl restore-original results\output.avi results\restored.mov
& $mjxl restore results\output.avi results\restored-mjpeg.avi
# 음성을 그대로 복사하고 JXL 압축 강도(기본값: 7)를 지정하는 선택 옵션.
& $mjxl encode input.mov results\copied-audio.avi --copy-audio --effort 8
```

`restore-original`은 원본 파일 전체를 복원하고 SHA-256을 검사한다. `restore`는 보존한 JPEG 패킷과 PCM 표본으로 MJPEG AVI를 만든다. `verify`는 원본 전체의 복원을 소스와 비교하므로 원본을 지우기 전에 실행한다. 복원 정보를 보존할 수 없으면 `encode`가 실패하며, `--no-original`로 명시적으로 생략한다. 이 옵션으로 만든 파일과 이전 버전의 AVI는 원본 전체를 복원할 수 없다.

FLAC 저장은 8/16/24비트 PCM을 지원하며 32비트 PCM은 PCM으로 둔다. 입력 음성은 PCM이거나 MJXL 메타데이터가 있는 FLAC이어야 하며, 다른 코덱은 `--copy-audio` 여부와 관계없이 거부한다. 원래 8비트 PCM은 16비트 FLAC의 상위 바이트에 저장하고 AVI 음성 메타데이터로 정확히 복원한다. 압축 강도는 1~10이고 기본값은 7이다. 높다고 항상 더 작은 파일이 되지는 않는다. 옵션은 명령 뒤에 쓴다. 상세한 규칙은 `mjxl --help`와 `mjxl encode --help`를 참고한다.

인코딩과 검증이 성공하면 음성·영상·복원 payload·순수 컨테이너의 크기를 나누어 출력한다. 헤더·인덱스·정렬 패딩과 복원 데이터 wrapper는 컨테이너 항목에 포함하고 복원 payload 자체는 별도로 센다. 원본의 복원 payload 기준은 0이며 네 항목의 합은 전체 파일 크기와 일치한다. 감소율이 음수이면 커진 것이고 기준 크기가 0이면 `n/a`로 표시한다.

`encode`는 출력 확장자 `.avi`·`.mkv`·`.mov`로 컨테이너를 선택하며 대소문자는 구분하지 않는다. 다른 출력 확장자는 거부한다. MKV는 FFmpeg의 Matroska 작성기로 `V_MS/VFW/FOURCC`와 `MJXL`을 기록하며, 원본 복원 정보는 MIME 형식이 `application/x-mjxl-original`인 `mjxl-original.bin` 첨부 데이터에 보관한다. FLAC 트랙의 `MJXL_ORIGINAL_BITS` 메타데이터에 원래 PCM 비트 깊이를 남긴다. `verify`·`restore-original`·`bench`는 세 컨테이너를 모두 읽는다. `restore`는 항상 MJPEG AVI를 쓰며 `.avi` 확장자가 필요하다. 같은 입력과 압축 강도에서 미디어 데이터는 같지만 컨테이너 비용은 다르다. 검사한 샘플 4개에서는 MKV가 AVI보다 970~21,638바이트 컸으며, 더 작은 컨테이너 비용을 보장하지 않는다.

MOV 샘플 4개의 두 음성 모드에서 MKV 변환, 원본 파일 전체 복원, FFmpeg 영상·음성 디코딩을 통과했다. 사용자는 4.2초 PCM·FLAC MKV의 팟플레이어 재생과 탐색이 정상임을 확인했다. 해당 MKV의 탐색기 썸네일은 표시되지 않았으며 Preview 창은 검사하지 않았다. 이번 변경은 썸네일 처리기를 설치하지 않는다. MKV의 CLI 왕복 검사를 반복하려면 `native/validate_cli.py`에 `--container mkv`를 추가하고 새 출력 폴더를 사용한다. 현재 MKV 구현은 Windows에서 검사했으며 Linux에서는 아직 재빌드하거나 검사하지 않았다.

## 재생과 호환성

| 경로 | 현재 지원 범위 |
| --- | --- |
| 팟플레이어 64비트 / DirectShow | 개발 PC에서 재생·탐색을 검증했다. 디코더 설치와 필터 캐시 갱신이 필요하다. |
| FFmpeg / ffplay | `libjxl` 디코더가 포함된 빌드와 명시적인 입력 디코더 지정이 필요하다. |
| VLC | 이 DirectShow 디코더를 사용하지 않으며 MJXL 재생은 구현하지 않았다. |
| Media Foundation | 실험용 MFT와 검사기가 있다. Windows Media Player 지원은 미완료이다. |
| 탐색기 썸네일 / 미리보기 | 설치된 처리기와 디코딩 경로에 따라 달라지며 DirectShow 설치만으로 지원을 보장하지 않는다. |

사용자가 겪은 이전 팟플레이어 코덱 선택 문제는 필터 캐시 삭제 후 해결됐다. `install.cmd`에 이 처리가 들어 있다. Media Foundation 등록과 MSIX 패키지는 별도 실험이며 이 설치 프로그램에 포함하지 않는다. Windows Media Foundation은 AVI 안의 FLAC을 읽을 때 멈추고 Windows에는 이를 푸는 기본 DirectShow 디코더가 없으므로, 이 경로에는 `--copy-audio`로 인코딩한다.

## Linux CLI와 FFmpeg

CLI는 C++20 컴파일러와 시스템 FFmpeg·libjxl·Brotli를 사용한다. Ubuntu 24.04 WSL2 x86-64와 GCC 13에서 검증했다. DirectShow와 MFT는 Windows 전용이다.

```bash
sudo apt install build-essential pkg-config libjxl-dev libbrotli-dev libflac-dev libavformat-dev libavcodec-dev libavutil-dev ffmpeg
git clone https://github.com/macosforge/alac.git _deps/alac
git -C _deps/alac checkout --detach c38887c5c5e64a4b31108733bd79ca9b2496d987
make -C native -j4
mkdir -p results
./dist/linux/mjxl encode input.mov results/output.avi
./dist/linux/mjxl verify input.mov results/output.avi
./dist/linux/mjxl restore-original results/output.avi results/restored.mov
sha256sum input.mov results/restored.mov
ffplay -vcodec libjxl results/output.avi
ffmpeg -v error -xerror -c:v libjxl -i results/output.avi -map 0:v:0 -map '0:a:0?' -f null -
```

실행할 배포판에서 빌드한다. 시스템 공유 라이브러리에 동적으로 링크하며 libjxl 버전에 따라 압축 크기도 달라질 수 있다. `ffmpeg -decoders`에서 `libjxl`을 확인한다. FFmpeg의 `-c:v libjxl`은 `-i` 앞에 쓰고 ffplay는 `-vcodec libjxl`을 사용한다. 이 명령은 원래 JPEG 파일 바이트가 아니라 픽셀을 디코딩한다. FFmpeg FourCC 매핑과 GStreamer 플러그인은 추가하지 않았다.

## 향후 재생 지원 계획

GStreamer(`gst`) 연동과 macOS 재생 지원을 추진할 계획이다. 둘 다 아직 구현하거나 검증하지 않았다. 현재 Windows DirectShow와 Linux FFmpeg 경로를 넘어 같은 가역 보존 파일을 재생할 수 있도록 하는 것이 목표이며, 기존 macOS 플레이어에서 지금 MJXL을 열 수 있다는 뜻은 아니다.

## 범위와 검증

네이티브 작성기는 고정 프레임 레이트, 영상 한 개, 음성 최대 한 개를 지원한다. 직접 구현한 AVI 작성기는 2GiB 미만의 classic AVI로 제한되며 MKV는 FFmpeg를 사용한다. 두 경로 모두 입력 패킷을 메모리에 보관하고 대용량 MKV는 검증하지 않았다. OpenDML·대용량 스트리밍·아티팩트 보정·움직임 예측은 구현하지 않았다.

기존 기록은 7개 샘플·1,332프레임의 JPEG·PCM 정확한 복원, 원본 파일 SHA-256, DirectShow 재생·탐색을 포함한다. Linux PCM·FLAC 변환에서 원본 복원과 FFmpeg 디코딩을 통과했다. Linux ffplay는 SDL 더미 출력으로 검사했으며 실제 화면 출력과 음성 청취는 검증하지 않았다. 과거 결과가 모든 파일과 PC에서의 성공을 보장하지는 않는다.

- [네이티브 도구와 상세 명령](native/README.ko.md)
- [검증 기록과 이전 작업 기록](STATUS.ko.md)
- [현재 작업 기록과 PCM 재생 잡음 조사](doc/WORKLOG.ko.md)
- Python 실험: [English](doc/experiments.md) / [한국어](doc/experiments.ko.md)
- 측정값: [COMPARISON.md](COMPARISON.md), [RESULTS.md](RESULTS.md)

소스와 빌드 정의는 Git에서 관리한다. 샘플·결과·`_deps`·`_build`·`dist`·생성된 `.sln`/`.slnx`/`.vcxproj`·서명 인증서는 넣지 않는다.

공개 헤더 주석은 영어 Doxygen으로, C/C++ 구현 주석은 한국어로 작성한다. 스크립트 메시지와 기본 문서는 영어로 작성한다. 한국어 문서는 `.ko.md`로 제공한다. 저장된 PCM이 정확해도 플레이어 출력까지 보장되지는 않는다. 팟플레이어는 18바이트 `strf`로 작성된 PCM AVI에서 주기적인 잡음을 냈다. 작성기는 이제 16바이트 `strf`를 쓰며, 이전에 만든 AVI는 다시 인코딩하기 전까지 기존 헤더를 유지한다.

FFmpeg 의존성 제거, MOV 입출력, GStreamer와 macOS 지원의 설계 및 단계별 완료 기준은 [이식성과 컨테이너 구현 계획](doc/PORTABILITY-PLAN.ko.md)에 정리한다.


첫 이식성 구현은 오디오에 libFLAC 1.5.0, 복원 hash에 PicoSHA2를 사용한다. CLI의 컨테이너 읽기와 MKV 쓰기는 아직 FFmpeg가 필요하다. `--audio-codec auto|flac|alac|copy`로 현재 오디오 경로를 선택하며 `--copy-audio`는 copy의 호환 별칭이다. 명시한 코덱의 미지원 형식과 상충 옵션은 오류로 처리한다. ALAC 인코딩·디코딩은 버전을 고정한 Apple 공개 구현을 사용한다. 자체 MOV 출력은 기본 ALAC, MKV는 명시적 ALAC을 지원한다. AVI ALAC은 아래의 private 실험 스위치를 요구한다.

FFmpeg SDK 없이 공통 라이브러리를 검사하는 방법은 다음과 같다.

```powershell
cmake -S . -B _build/portable-codecs -DMJXL_BUILD_CLI=OFF -DMJXL_BUILD_WINDOWS_ADAPTERS=OFF -DMJXL_BUILD_TESTS=ON
cmake --build _build/portable-codecs --config Release --target mjxl_portability_test
ctest --test-dir _build/portable-codecs -C Release --output-on-failure
```

Windows 의존성은 `native/prepare_deps.ps1`로 준비한다. system-library CMake 경로는 libjxl·Brotli·libFLAC 개발 패키지가 필요하며 전환 중인 CLI에는 FFmpeg 개발 라이브러리도 필요하다.


## ALAC과 자체 MOV 출력

MOV는 자체 writer와 ALAC을 기본으로 사용하며 --copy-audio로 PCM을 선택한다. MKV는 명시적 ALAC을 지원하되 기본 FLAC을 유지한다. AVI ALAC은 아래 두 옵션을 모두 요구하는 private 실험이다. FFmpeg가 AVI의 오디오를 자동 식별하지 못하므로 Windows 플레이어 호환성을 전제하지 않는다.

```powershell
& $mjxl encode input.mov output.mov
& $mjxl encode input.mov output.mkv --audio-codec alac
& $mjxl encode input.mov experimental.avi --audio-codec alac --experimental-avi-alac
```

세 컨테이너 모두 원본 전체 복원 정보를 유지한다. 명시한 코덱·채널 배치가 미지원이면 오류로 처리하고 MOV auto는 ALAC 프로필 밖의 PCM을 그대로 복사한다. 검사한 PCM8 카메라 자료처럼 ALAC이 FLAC이나 PCM보다 클 수 있다. [실측 비교](doc/AUDIO-CONTAINERS.ko.md)와 [MOV·ALAC 프로필](doc/MOV-ALAC-PROFILE.ko.md)을 참고한다. Windows FFmpeg 검증과 별개로 QuickTime Player·macOS 재생과 썸네일은 아직 검증하지 않았다. prepare_deps.ps1은 버전을 고정한 Apple ALAC 소스도 준비한다. 현재 Linux/macOS 빌드에도 해당 소스와 CMake 3.28 이상이 필요하다.
