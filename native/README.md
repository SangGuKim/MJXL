# MJXL 네이티브 인코더와 DirectShow 디코더

VS2026 x64로 빌드하는 실험용 코덱이다. 영상은 프레임별 JPEG XL 가역 재압축이며 PCM 음성은 원래 샘플을 그대로 복사한다. AVI의 실험용 FourCC는 `MJXL`이다. 각 영상 패킷은 JPEG 복원 메타데이터를 포함한 독립 JXL 파일이다.

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

## 변환·복원·검증

```powershell
$mjxl = 'W:\proj\mjxl\dist\Release\mjxl.exe'
& $mjxl encode W:\proj\mjxl\samples\IMG_20040318_225819.MOV W:\proj\mjxl\results\example.avi
& $mjxl verify W:\proj\mjxl\samples\IMG_20040318_225819.MOV W:\proj\mjxl\results\example.avi
& $mjxl restore W:\proj\mjxl\results\example.avi W:\proj\mjxl\results\restored-mjpeg.avi
& $mjxl bench W:\proj\mjxl\results\example.avi
```

`encode`는 프레임마다 원래 JPEG의 정확한 복원을 확인하고, 파일 작성 후 다시 AVI를 읽어 JPEG 패킷과 PCM 샘플 전체를 비교한다. `restore`는 원래 JPEG 패킷을 MJPEG AVI로 돌려 놓는다. JPEG를 픽셀로 디코딩했다가 재인코딩하지 않는다. 기존 출력 파일은 덮어쓰지 않는다. 검증 전에는 `.partial` 이름을 사용하고 성공하면 최종 이름으로 바꾼다.

MJXL AVI는 원래 JPEG 패킷과 PCM 샘플의 보존본이다. 원본 MOV의 메타데이터·배치·파일 바이트 전체를 재현하는 형식은 아니다. 원래 MOV 파일 전체를 보존하려면 원본 또는 루트의 `mjxl_lab.py` 아카이브를 함께 보관한다.

## 팟플레이어와 DirectShow

```powershell
& W:\proj\mjxl\native\register.ps1
& W:\proj\mjxl\dist\Release\mjxl_graph_test.exe W:\proj\mjxl\dist\Release\mjxl_decoder.ax W:\proj\mjxl\results\example.avi
```

`register.ps1`은 현재 사용자에게만 디코더를 등록한다. 관리자 권한은 필요하지 않다. 등록 해제는 `register.ps1 -Unregister`로 실행한다. 등록 후 플레이어를 다시 열고 AVI를 재생한다. 다른 코덱의 우선순위나 파일 연결은 변경하지 않는다.

DirectShow 디코더는 JXL을 RGB24로 직접 디코딩한다. JPEG 복원 뒤 다시 JPEG를 디코딩하는 경로를 사용하지 않는다. 모든 프레임이 독립 프레임이므로 임의 탐색을 위해 앞선 프레임을 복원할 필요가 없다. 기본 그래프 검사는 Windows AVI Splitter와 Null Renderer를 사용해 영상·PCM 입력과 중간 탐색을 확인한다. 마지막 인자에 `--registered`를 추가하면 COM 등록을 통한 필터 생성을 검사한다. `--auto`를 추가하면 DirectShow의 자동 필터 선택과 실제 렌더러로 재생·탐색을 검사하며 ActiveMovie 창과 음성 출력이 발생할 수 있다.

VLC는 자체 디코더를 사용하므로 DirectShow 필터 등록만으로 MJXL 재생을 지원하지 않는다. 탐색기 미리보기 역시 사용 중인 미리보기 처리기의 디코딩 경로에 따라 달라진다.

## FFmpeg에서 사용

현재 SDK의 FFmpeg에는 `libjxl` 디코더가 있다. MJXL FourCC 자동 매핑은 없으므로 입력 디코더를 명시한다.

```powershell
& "$env:FFMPEG_SDK\bin\ffmpeg.exe" -c:v libjxl -i W:\proj\mjxl\results\example.avi -map 0:v:0 -map 0:a:0 -f null -
```

이 명령은 실제 픽셀·음성 디코딩 검사이다. 일반 FFmpeg 인코더로 MJPEG를 픽셀로 푼 뒤 JXL을 생성하는 것은 이 도구의 JPEG 가역 재압축과 다르다. JPEG 패킷 보존 변환은 `mjxl.exe encode`를 사용한다.

## 첫 버전의 범위

- 고정 프레임 레이트, 영상 한 개, PCM 음성 최대 한 개를 지원한다.
- PCM은 unsigned 8-bit 또는 signed little-endian 16/24/32-bit를 지원한다. 현재 샘플 검증 범위는 PCM8 mono이다.
- 파일당 2GiB 미만의 classic AVI를 작성한다. OpenDML은 아직 지원하지 않는다.
- 0이 아닌 스트림 시작 오프셋, 가변 프레임 레이트, 추가 스트림은 누락시키지 않고 거부한다.
- 입력 패킷을 메모리에 보관하는 첫 구현이다. 대용량 스트리밍 변환은 후속 대상이다.
- 아티팩트 보정과 프레임 간 예측은 포함하지 않는다.
- 디코더 파일이 플레이어나 미리보기 프로세스에서 사용 중이면 해당 프로그램을 닫고 빌드한다. 잠긴 파일을 강제로 교체하지 않는다.

자동 검증은 `native/validate_native.py`로 실행한다. 원본과 출력 샘플은 `samples/`, `results/native-v1/`에 있으며 Git 대상에서 제외한다.
