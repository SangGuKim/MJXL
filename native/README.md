# MJXL 네이티브 인코더와 DirectShow 디코더

VS2026 x64로 빌드하는 실험용 코덱이다. 영상은 프레임별 JPEG XL 가역 재압축이며 PCM 음성은 기본적으로 그대로 복사한다. `--flac`을 지정하면 FLAC으로 무손실 압축한다. AVI의 실험용 FourCC는 `MJXL`이다. 각 영상 패킷은 JPEG 복원 메타데이터를 포함한 독립 JXL 파일이다.

## 빌드와 솔루션

프로젝트 루트의 `MJXL.sln`을 VS2026에서 열면 된다. 기존 `.sln` 형식으로 제공하며 `.slnx`도 빌드 폴더에 생성한다. 최초 구성과 의존성 준비는 다음과 같이 실행한다.

```powershell
# FFmpeg SDK는 기존 환경변수를 사용한다.
$env:FFMPEG_SDK
& W:\proj\mjxl\native\prepare_deps.ps1
& W:\proj\mjxl\native\build.ps1
# 다른 PC의 기존 캐시와 분리해서 빌드할 때 사용한다.
# & W:\proj\mjxl\native\build.ps1 -BuildDirectory _build\vs2026-local
```

VS2026의 v145 도구 집합과 Windows SDK가 필요하다. FFmpeg 헤더·import library·DLL은 `$env:FFMPEG_SDK`에서 읽는다. CMake 4.1 이상과 sgtk 환경의 Python을 솔루션 생성에 사용한다. PATH에 CMake가 없으면 VS2026에 포함된 CMake를 사용한다. FFMPEG_SDK를 우선하며 이전 FFMPEG_PATH도 호환 입력으로 받는다. 실행 파일과 디코더 자체에는 Python이 필요하지 않다.

libjxl 0.11.2와 필수 하위 의존성은 `_deps/libjxl`에서 정적으로 빌드한다. C++ 런타임도 정적으로 포함한다. 다른 프로그램이 먼저 불러온 구형 MSVCP140.dll과 충돌하지 않게 하기 위한 구성이다. Microsoft DirectShow 기본 클래스 원본은 보존하고, VS2026 호환 수정 두 곳을 빌드 폴더 사본에만 적용한다.

## Linux 빌드

`mjxl` 명령줄 도구는 GCC나 Clang으로 빌드할 수 있다. 시스템의 FFmpeg, libjxl, Brotli를 사용한다. DirectShow 필터와 Media Foundation 디코더는 Windows 전용이다. 결과는 `dist/linux/mjxl`에 생성한다.

```bash
sudo apt install build-essential pkg-config libjxl-dev libbrotli-dev libavformat-dev libavcodec-dev libavutil-dev
make -C native
```

Ubuntu 24.04 WSL2 x86-64의 GCC 13으로 전체 링크와 실행을 검증했다. 실행 파일은 시스템 공유 라이브러리에 동적으로 연결되므로 모든 배포판에서 그대로 실행되는 단일 바이너리는 아니다. libjxl 버전이 다르면 압축 결과 크기가 달라질 수 있다. C++20과 `std::format`을 지원하는 표준 라이브러리가 필요하다.

```bash
mkdir -p results
./dist/linux/mjxl encode input.mov results/output.avi --flac
./dist/linux/mjxl verify input.mov results/output.avi
./dist/linux/mjxl restore-original results/output.avi results/restored.mov
sha256sum input.mov results/restored.mov
# 샘플은 별도로 준비하며 출력 폴더는 새 경로를 지정한다.
python3 native/validate_cli.py --binary dist/linux/mjxl --samples samples --output results/linux-check
```

Linux 빌드는 CLI만 제공하며 GStreamer·FFmpeg용 재생 플러그인을 등록하지 않는다.

## 변환·복원·검증

```powershell
$mjxl = 'W:\proj\mjxl\dist\Release\mjxl.exe'
& $mjxl encode W:\proj\mjxl\samples\IMG_20040318_225819.MOV W:\proj\mjxl\results\example.avi
& $mjxl verify W:\proj\mjxl\samples\IMG_20040318_225819.MOV W:\proj\mjxl\results\example.avi
& $mjxl restore W:\proj\mjxl\results\example.avi W:\proj\mjxl\results\restored-mjpeg.avi
& $mjxl bench W:\proj\mjxl\results\example.avi
```

`encode`는 프레임마다 원래 JPEG의 정확한 복원과 FLAC의 PCM 복원을 확인하고, 파일 작성 후 다시 AVI를 읽어 JPEG 패킷과 PCM 샘플 전체를 비교한다. `mjxl --help`와 `mjxl encode --help`로 명령과 옵션을 볼 수 있다. 옵션은 명령마다 다르며 명령 뒤 어느 위치에 써도 된다. 명령 앞에는 쓸 수 없다. `--flac`을 붙이면 음성을 FLAC으로 저장한다. 현재 샘플에서 음성을 약 34% 줄이지만 파일 전체 절감은 약 0.365%이다. `-e 1-10` 또는 `--effort 1-10`은 JXL 압축 강도를 정하며 기본값은 7이다. 21프레임 측정에서 8은 약 3배 느리고 약 0.2%포인트 더 줄었다. 9와 10은 30배 이상 느리지만 더 줄지 않았다. `restore`는 원래 JPEG 패킷을 MJPEG AVI로 돌려 놓는다. JPEG를 픽셀로 디코딩했다가 재인코딩하지 않는다. 기존 출력 파일은 덮어쓰지 않는다. 검증 전에는 `.partial` 이름을 사용하고 성공하면 최종 이름으로 바꾼다.

`encode`는 영상·음성 패킷을 뺀 원본 파일의 나머지 바이트와 패킷 배치를 Brotli로 압축해 AVI 끝의 `mjxo` 청크에 저장한다. 현재 샘플의 비용은 파일당 약 1.4~6.2 KiB이다. 파일 이름을 확정하기 전에 저장된 AVI만으로 원본 전체가 복원되는지 확인한다. `restore-original`은 원본 파일을 바이트 단위로 복원하고 SHA-256을 검사한다. `verify`도 원본 파일 전체를 비교하므로 원본을 지우기 전에 이 명령으로 확인한다. 패킷이 입력 파일과 일치하지 않는 등 복원 정보를 만들 수 없으면 `encode`가 실패한다. 복원 정보 없이 인코딩하려면 `--no-original`을 지정한다. 이 옵션으로 만든 파일과 이전 도구로 만든 AVI는 원본을 복원할 수 없으며, `verify`는 `--no-original`을 지정하지 않으면 이런 파일을 실패로 처리한다.

```powershell
& $mjxl restore-original W:\proj\mjxl\results\example.avi W:\proj\mjxl\results\restored.MOV
```

`--flac`은 FFmpeg FLAC 인코더를 사용한다. 이 인코더는 8비트를 지원하지 않으므로 8비트 PCM은 16비트 FLAC의 상위 바이트에 저장하고, 원래 비트는 `hdrl` 안의 `mjxa` 청크에 기록한다. 24비트는 24비트 FLAC으로 저장하고 32비트 PCM은 PCM으로 유지한다. `restore`와 `restore-original`은 이 정보로 원래 PCM 바이트를 되돌리며, `mjxa`가 없는 FLAC AVI는 거부한다.

`restore`는 재생과 편집을 위한 MJPEG AVI를 만들고 원본 컨테이너를 재현하지 않는다. 원본 파일 전체가 필요하면 `restore-original`을 사용한다.

## 다른 PC에 설치

작업 드라이브를 빼도 동작하도록 빌드된 `dist/Release`에서 현재 사용자의 로컬 폴더로 복사하고 등록한다. 관리자 권한은 필요하지 않다. 기본 위치는 `%LOCALAPPDATA%\MJXL`이다. 드라이브 문자는 스크립트 위치로 찾으므로 어느 드라이브에서 실행해도 된다.

```bat
W:\proj\mjxl\native\install.cmd
W:\proj\mjxl\native\install.cmd -DecoderOnly
W:\proj\mjxl\native\install.cmd -Uninstall
```

디코더, `mjxl.exe`, 필요한 FFmpeg DLL과 라이선스를 복사하고 디코더를 등록한 뒤 DirectShow 필터 캐시를 지운다. `-DecoderOnly`는 재생에 필요한 디코더만 설치한다. `-Destination`으로 위치를 바꿀 수 있다. `-Uninstall`은 설치 기록에 있는 파일만 지운다. 64비트 플레이어에서만 동작한다. 다시 빌드한 뒤에는 `install.cmd`를 다시 실행해야 로컬 복사본이 바뀐다. `register.ps1`은 개발용으로 빌드 폴더의 디코더를 등록하므로 드라이브를 빼면 재생되지 않는다.

## 팟플레이어와 DirectShow

```powershell
& W:\proj\mjxl\native\register.ps1 -AllUsers
& W:\proj\mjxl\dist\Release\mjxl_graph_test.exe W:\proj\mjxl\dist\Release\mjxl_decoder.ax W:\proj\mjxl\results\example.avi
```

`register.ps1 -AllUsers`는 모든 사용자용으로 디코더를 등록하며 필요한 경우 Windows 관리자 승인창을 표시한다. 등록 해제는 `register.ps1 -AllUsers -Unregister`로 실행한다. `-AllUsers`를 생략하면 관리자 권한 없이 현재 사용자에게만 등록한다. 현재 PC에서는 필터 캐시를 새로 만든 뒤 현재 사용자용 등록만으로 팟플레이어 재생을 확인했다. 등록 후에도 자동 선택이 실패하면 오래된 사용자별 ActiveMovie 필터 캐시 여부를 확인한다. 등록 후 플레이어를 다시 열고 AVI를 재생한다. 다른 코덱의 우선순위나 파일 연결은 변경하지 않는다.

DirectShow 디코더는 JXL을 RGB24로 직접 디코딩한다. JPEG 복원 뒤 다시 JPEG를 디코딩하는 경로를 사용하지 않는다. 모든 프레임이 독립 프레임이므로 임의 탐색을 위해 앞선 프레임을 복원할 필요가 없다. 기본 그래프 검사는 Windows AVI Splitter와 Null Renderer를 사용해 영상·PCM 입력과 중간 탐색을 확인한다. 마지막 인자에 `--registered`를 추가하면 DLL을 미리 로드하지 않고 COM 등록을 통한 필터 생성을 검사한다. `--auto`를 추가하면 DLL 사전 로드나 디코더 직접 생성 없이 DirectShow의 자동 필터 선택과 실제 렌더러로 재생·탐색을 검사하며 ActiveMovie 창과 음성 출력이 발생할 수 있다.

`--flac` 파일의 경우 Windows 자체 DirectShow에는 AVI 안의 FLAC을 푸는 디코더가 없어 자동 재생에서 음성이 연결되지 않는다. 팟플레이어는 자체 디코더로 재생한다. VLC는 자체 디코더를 사용하므로 DirectShow 필터 등록만으로 MJXL 재생을 지원하지 않는다. 탐색기 미리보기 역시 사용 중인 미리보기 처리기의 디코딩 경로에 따라 달라진다.

## FFmpeg에서 사용

현재 SDK의 FFmpeg에는 `libjxl` 디코더가 있다. MJXL FourCC 자동 매핑은 없으므로 입력 디코더를 명시한다.

```powershell
& "$env:FFMPEG_SDK\bin\ffmpeg.exe" -c:v libjxl -i W:\proj\mjxl\results\example.avi -map 0:v:0 -map 0:a:0 -f null -
```

이 명령은 실제 픽셀·음성 디코딩 검사이다. 일반 FFmpeg 인코더로 MJPEG를 픽셀로 푼 뒤 JXL을 생성하는 것은 이 도구의 JPEG 가역 재압축과 다르다. JPEG 패킷 보존 변환은 `mjxl.exe encode`를 사용한다.

## 첫 버전의 범위

- 고정 프레임 레이트, 영상 한 개, 음성 최대 한 개를 지원한다.
- 입력 PCM은 unsigned 8-bit 또는 signed little-endian 16/24/32-bit를 지원한다. 실제 샘플은 PCM8 mono이고 그 외 형식은 합성 음성으로 검증했다.
- 파일당 2GiB 미만의 classic AVI를 작성한다. OpenDML은 아직 지원하지 않는다.
- 0이 아닌 스트림 시작 오프셋, 가변 프레임 레이트, 추가 스트림은 누락시키지 않고 거부한다.
- 입력 패킷을 메모리에 보관하는 첫 구현이다. 대용량 스트리밍 변환은 후속 대상이다.
- 아티팩트 보정과 프레임 간 예측은 포함하지 않는다.
- 디코더 파일이 플레이어나 미리보기 프로세스에서 사용 중이면 해당 프로그램을 닫고 빌드한다. 잠긴 파일을 강제로 교체하지 않는다.

자동 검증은 `native/validate_native.py`로 실행하며 `--flac`을 주면 FLAC 저장을 검증한다. 각 샘플의 `restore-original` 결과를 원본과 SHA-256으로 비교한다. 원본과 출력 샘플은 `samples/`, `results/native-original/`, `results/native-original-flac/`에 있으며 Git 대상에서 제외한다.

## Media Foundation 실험 구현

`mjxl_mft.dll`과 `mjxl_mft_test.exe`는 Media Foundation용 실험 구현이다. 기존 7개 AVI의 1,332프레임을 로컬 MFT 경로로 검증했다. JPEG 바이트 복원과 별도로, RGB32 출력과 탐색·타임스탬프·오류 처리를 검사한다. `mjxl_mft_test.exe <MJXL AVI> --local <mjxl_mft.dll>`은 전역 등록 없이 실행한다.

`register_mft.ps1`은 관리자 승인 후 MFT를 Program Files에 복사하고 데스크톱용으로 등록한다. `-Unregister`는 등록을 해제한다. 이 등록만으로 Windows Media Player에서 재생되지는 않았다. Media Player 지원은 미완료이며 현재 권장 재생 경로는 DirectShow이다. Windows Media Foundation의 AVI 소스는 AVI 안의 FLAC을 읽을 때 멈추므로, 이 경로를 시험할 때는 `--flac` 없이 만든 AVI를 사용한다.

`package_mft.ps1`은 Windows SDK로 미디어 확장 MSIX를 생성한다. `-Sign`은 현재 사용자 My 저장소에 30일짜리 개발용 코드 서명 인증서를 만들거나 재사용한다. 신뢰 저장소 등록이나 패키지 설치는 수행하지 않는다. 개발 패키지는 Media Player에서 설치·재생 검증하지 않았으며 배포용 설치 프로그램으로 간주하지 않는다. 인증서·개인 키·서명 패키지는 Git에 포함하지 않는다.
