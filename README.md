# MJXL 원본 보존 실험

이 문서는 MJPEG의 JPEG 패킷을 가역적으로 압축하고 원본 MOV 파일 전체를 정확히 복원하는 Python 연구 도구를 설명한다. VS2026으로 빌드하는 실제 변환 실행 파일과 DirectShow 디코더는 [네이티브 사용 방법](native/README.md)을 참고한다. 프로젝트 루트의 `MJXL.sln`이 VS2026 솔루션이다.

## 실험 방식

| 모드 | 독립 프레임 | 참조 프레임 |
| --- | --- | --- |
| jxl | JPEG XL 가역 재압축 | 없음 |
| inter | GOP 첫 프레임에 JXL | 직전 JPEG의 마지막 32 KiB를 DEFLATE 사전으로 사용 |
| coeff | GOP 첫 프레임에 JXL | 동일 위치의 양자화된 DCT 계수 차이를 int16으로 저장하고 DEFLATE 압축 |
| hybrid | GOP 첫 프레임에 JXL | JXL, 사전, 계수 방식 중 현재 프레임의 저장 크기가 가장 작은 방식 선택 |

기본 GOP 길이는 12이다. 혼합 선택은 프레임별 탐욕적 선택이며 전역 최적화가 아니다. 참조 대상은 항상 직전 프레임의 복원된 JPEG이다. GOP 경계에서는 참조를 사용하지 않는다.

계수 방식은 단일 인터리브 스캔의 8비트 baseline JPEG를 대상으로 한다. DC는 절대 계수로 복원한 뒤 차이를 계산한다. AC는 지그재그 순서의 동일 위치 계수를 비교한다. 움직임 보상과 엔트로피 모델 최적화는 구현하지 않았다.

원래 JPEG를 비트 단위로 재현하기 위해 헤더, 원래 허프먼 심벌 순서, 엔트로피 스트림의 마지막 패딩 비트, 스캔 뒤 데이터를 함께 저장한다. 따라서 디코딩된 픽셀만 같도록 보존하는 방식과 다르다. 계수 파서가 원래 JPEG를 정확히 재현하지 못하거나 지원하지 않는 JPEG이면 해당 P 후보를 제외하고 JXL로 대체한다. JXL 자체가 처리하지 못하는 입력은 실험을 중단한다.

MOV에서 영상 패킷을 제외한 모든 구간을 DEFLATE로 저장한다. 이 구간에는 음성, MOV 메타데이터, 패딩 등이 포함된다. 오디오 압축률은 영상 압축률과 구분해서 평가해야 한다. 패킷 위치와 바이트가 원본 파일과 일치하는지 먼저 검사한다.

## 실행

현재 환경은 conda sgtk의 Python 3.13, PyAV, NumPy, libjxl 0.11.2를 사용한다. 별도 패키지 설치는 하지 않았다.

```powershell
$env:PYTHONUTF8 = '1'
$python = "$env:USERPROFILE\miniforge3\envs\sgtk\python.exe"
$jxl = "$env:USERPROFILE\miniforge3\envs\sgtk\Library\bin"
& $python -X utf8 W:\proj\mjxl\mjxl_lab.py --dll-dir $jxl bench W:\proj\mjxl\samples\IMG_20040318_225819.MOV W:\proj\mjxl\results\new-run --gop 12
& $python -X utf8 W:\proj\mjxl\mjxl_lab.py --dll-dir $jxl restore W:\proj\mjxl\results\new-run\IMG_20040318_225819.hybrid.mjxl-lab W:\proj\mjxl\results\restored.MOV
```

결과 파일은 기존 파일을 덮어쓰지 않는다. 재실행할 때 새 결과 폴더를 지정한다. 소스 영상은 수정하지 않는다.

## 검증과 파일 형식

실험마다 모든 JXL 프레임과 사용 가능한 계수 후보를 즉시 역변환하여 원본 JPEG와 비교한다. 이어서 각 모드의 아카이브를 실제로 저장하고 다시 읽어 전체 원본 파일과 비교한다. 각 레코드와 전체 파일의 SHA-256도 확인한다.

실험 아카이브는 `MJXLLAB1` 식별자 8바이트, little-endian uint64 JSON 헤더 길이, UTF-8 JSON 헤더, 순서대로 연결한 레코드 페이로드로 구성된다. 헤더에는 원본 파일 길이와 해시, 레코드별 코덱·원본 길이·저장 길이·해시가 들어간다. JPEG XL은 JPEG 복원 메타데이터를 포함하는 독립 JXL 파일로 저장한다. 계수 페이로드는 DEFLATE 해제 후 `<IIIIBB`로 헤더·심벌·접미부·계수 바이트 길이와 패딩 비트 수·값을 읽고 각 영역을 순서대로 읽는다. 계수 차이는 little-endian int16 배열이다.

현재 전체 파일과 후보들을 메모리에 보관하므로 대용량 영상용 구현은 아니다. 입력 파일은 신뢰하는 연구용 샘플로 제한한다. 랜덤 탐색용 인덱스, 실시간 재생 경로, 입력 자원 제한, FFmpeg 및 DirectShow 통합은 별도 구현 대상이다.

## 픽셀 디코딩 성능 측정

`decode_bench.py`는 MJPEG와 JXL을 RGB8 픽셀로 디코딩하는 속도를 측정한다. 예열 후 3회 실행의 중앙값과 프레임별 p95·최대 지연을 기록한다. JXL은 매 프레임 새 디코더를 생성하며 다중 스레드 측정에서는 작업 풀을 재사용한다. 입력은 메모리에 올려 두므로 디스크와 렌더링 성능을 포함하지 않는다. 측정 전에 JXL이 원래 JPEG로 정확히 복원되는지 확인한다.

```powershell
& $python -X utf8 W:\proj\mjxl\decode_bench.py --dll-dir $jxl W:\proj\mjxl\samples\IMG_20060430_203434.MOV - W:\proj\mjxl\results\decode-new.json
& $python -X utf8 W:\proj\mjxl\summarize_results.py W:\proj\mjxl
```

아카이브 인자로 `-`를 주면 메모리에서 JXL을 생성한 뒤 측정한다. 기존 JXL 독립 프레임 아카이브 경로를 지정해도 된다. 집계 문서는 `COMPARISON.md`에 저장한다.

## 다음 설계 판단

정적 장면, 카메라 이동, 장면 전환, 저조도 노이즈가 있는 샘플을 추가해서 비교한다. 계수 방식의 압축률은 헤더와 심벌 보존 비용까지 포함해서 판단한다. 이후 블록 이동 예측, 계수 주파수별 예측과 압축, GOP 최적화, 처리 속도를 검토한다. P프레임 예측이 불리한 입력에서도 JXL 독립 프레임을 선택할 수 있는 구조를 유지한다.
