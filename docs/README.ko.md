<p align="center">
  <img src="../resources/app-256.png" width="96" alt="VRChat DLSS5 Cam 아이콘">
</p>

# VRChat DLSS5 Cam

[English](../README.md) · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · **한국어**

VRChat DLSS5 Cam은 VRChat 내장 카메라(*Spout Stream*을 켠 Stream 카메라)의 화면을 가져와
**DLSS 5 뉴럴 렌더링(DLSSNR)** 으로 실시간 처리하고, 결과를 바로 보여주며 무손실 PNG 사진으로 저장합니다.
독립 실행형 Windows 앱이므로 VRChat에 아무것도 주입하지 않으며 모드도 필요 없습니다.

> **상태: 초기 프리뷰.** 바이너리는 GitHub Actions에서 빌드되며, 파이프라인은 아직 다양한 GPU와 DLSS 5 런타임 빌드에서
> 충분히 검증되지 않았습니다. 문제가 있으면 `log.txt`를 첨부하여 이슈를 열어 주세요.

## 기능

- DLSS 5 처리 후의 VRChat 카메라 화면을 **실시간 미리보기**. 와이프 비교, 원본, 모션 벡터, 깊이 보기 지원.
- `nvngx_dlssnr.dll`을 직접 호스팅하는 **DLSS 5 뉴럴 렌더링**. RenoDX의 DLSS 5 ReShade 애드온과 같은 파라미터
  (프리셋, 스타일, 강도, 전역 톤, 지역 톤, 지역 구조, 피부 구조, 자동 마스크, UI 보정)와 출력 블렌드(입력 노출, 톤 전달 강도, 색 강도)를 제공합니다.
  변경 사항은 즉시 적용되며 정지된 프레임에서도 조정할 수 있습니다.
- **정지 이미지 처리.** 사진이나 스크린샷(PNG, JPEG, BMP, TIFF, GIF, WebP, HEIC…)을 열거나 창에 끌어다 놓으세요.
  DLSS 5가 여러 패스에 걸쳐 다듬고, *처리 후 PNG 저장*으로 결과를 촬영 폴더에 저장합니다.
- **프레임 가이던스.** DLSSNR은 시간적 모델이므로 모션 벡터와 깊이가 필요합니다.
  정방향/역방향 일관성 검사가 있는 **NVIDIA Optical Flow** 모션 벡터와 **Depth Anything V2**가 GPU에서 추정한
  깊이 맵(ONNX Runtime + DirectML)을 전달합니다. GPU 블록 매칭과 대체 깊이는 폴백으로 남아 있으며, 뉴럴 렌더링 전에
  **DLAA**(네이티브 해상도 DLSS 안티앨리어싱) 패스를 추가할 수 있습니다.
- **적응형 해상도.** 송신자 해상도를 자동 감지합니다. 사용자 지정 출력 해상도를 설정할 수 있으며 DLSS 5가 직접 업스케일하도록 할 수 있습니다.
  선택적으로 장면 전환을 감지하여 시간 이력을 재설정할 수 있습니다.
- **무손실 촬영.** 처리된 프레임(선택적으로 원본도)을 PNG로 저장. VRChat이 전면에 있어도 동작하는 전역 단축키
  (기본 `Ctrl+Alt+P`)와 타임랩스 모드 지원.
- **4개 언어**(English, 简体中文, 日本語, 한국어), Windows UI 언어에 따라 자동 선택.
- **반응성 좋은 인터페이스.** 창은 별도의 스레드와 GPU 큐에서 실행되므로 4K 뉴럴 패스가 수십 밀리초 걸려도 미리보기와 컨트롤이 부드럽게 유지됩니다.
- 모니터별 DPI 지원, 다크 테마 UI, GPU 타이머, 내장 로그.

## 요구 사항

| | |
|---|---|
| OS | Windows 10 21H2 / Windows 11, 64비트 |
| GPU | NVIDIA GeForce RTX(DLSS 5 뉴럴 렌더링은 RTX 하드웨어 전용), 최신 Game Ready 드라이버 |
| VRChat | Stream 카메라에 *Spout Stream* 옵션이 있는 빌드(데스크톱 또는 VR) |
| DLSS 5 런타임 | 직접 준비한 `nvngx_dlssnr.dll`. **이 프로젝트에는 포함되지 않으며 다운로드하지도 않습니다.** |

## 설정 방법

1. [Releases](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases)에서 `VRChatDLSS5Cam-win64.zip`을 내려받아 원하는 위치에 압축을 풉니다.
2. `nvngx_dlssnr.dll`을 압축 해제한 폴더(`VRChatDLSS5Cam.exe` 옆)에 복사합니다. *DLSS 5 뉴럴 렌더링 → 런타임 경로*에서 파일을 지정할 수도 있습니다.
3. VRChat을 실행하고 **카메라**를 연 뒤 카메라 모드를 **Stream**으로 전환하고, Stream 카메라 설정에서 **Spout Stream**을 켭니다. VRChat은 카메라 화면을 Spout 송신자(`VRCSender1`)로 내보냅니다.
4. `VRChatDLSS5Cam.exe`를 실행합니다. 송신자가 자동으로 연결되고 처리된 화면이 미리보기에 표시됩니다.
5. VRChat에서 구도를 잡고 **Ctrl+Alt+P**(또는 *촬영* 버튼)를 누릅니다. PNG는 기본적으로 `사진\VRChat DLSS5 Cam`에 저장됩니다.

팁
- Spout 스트림 해상도는 VRChat이 결정합니다. VRChat의 `config.json`에서 `camera_spout_res_width` / `camera_spout_res_height`를 높이면 더 높은 해상도의 입력을 얻을 수 있으며 앱이 자동으로 맞춥니다.
- 다른 출력 크기를 원하면 *소스* 섹션에서 *사용자 지정 해상도*를 켜세요. *DLSS 5 업스케일*을 켜면 DLSSNR이 더 큰 이미지를 직접 렌더링합니다.
- *와이프* 비교 모드에서 미리보기의 핸들을 드래그하면 각 파라미터의 효과를 확인할 수 있습니다.
- 실시간 카메라 대신 이미지를 처리하려면 *소스* 섹션의 *입력 소스*를 *이미지 파일*로 바꾸고 이미지를 여세요(파일을 창에 끌어다 놓아도 됩니다).
  슬라이더를 조정한 뒤 *처리 후 PNG 저장*(또는 단축키)을 누르면 결과가 `<이름>_DLSS5_<너비>x<높이>.png`로 촬영 폴더에 저장됩니다. 긴 변이 8192 px를 넘는 이미지는 축소하여 처리합니다.

## 파라미터

| 섹션 | 설정 | 의미 |
|---|---|---|
| 소스 | 입력 소스 | *VRChat 카메라(Spout)* 또는 *이미지 파일*. |
| 소스 | 페이퍼 화이트 / 하이라이트 압축 | 부동소수점(선형 HDR) Spout 텍스처일 때만 표시됩니다. SDR 뉴럴 패스 전에 적용되는 노출 기준과 하이라이트 롤오프. |
| DLSS 5 | 프리셋 | DLSSNR에 전달하는 렌더 프리셋 힌트(0–3). |
| DLSS 5 | 스타일 | `DLSSNR.Style`: 기본 / 자연 / 시네마틱. |
| DLSS 5 | 강도 | 뉴럴 패스의 전체 강도(0–2). 1까지는 런타임 자체의 강도이며, 1을 넘으면 앱이 네트워크가 만든 변화 전체를 증폭합니다(아티팩트도 커질 수 있음). |
| DLSS 5 | 전역 톤 / 지역 톤 | 전역 및 지역 톤 강도(0–2). 1까지는 런타임에 전달되고, 1을 넘으면 앱이 네트워크 변화 중 해당 부분(전역 톤 커브 = 원본 밝기에 따른 밝기와 색, 또는 지역 저주파 조명)을 증폭합니다. |
| DLSS 5 | 지역 구조 / 피부 구조 | 디테일 강화(0–2). 1을 넘으면 변화 중 세부 성분을 증폭합니다(피부 구조는 피부색 영역에만). 피부 구조는 런타임 기본값으로 둘 수 있습니다. |
| DLSS 5 | 자동 마스크 / UI 보정 | 자동 피사체 마스크, UI 안전 처리. |
| DLSS 5 | 입력 노출 / 톤 전달 강도 / 색 강도 | 출력 블렌드. 입력 노출(0.25–4배)은 네트워크가 보는 화면을 배율 조정하며(페이퍼 화이트 배율에 해당) 이후 되돌려집니다. 톤 전달 강도와 색 강도(0–2)는 뉴럴 패스가 바꾼 밝기와 색이 출력에 얼마나 반영되는지를 선형 광에서 정합니다. 1 / 1이면 뉴럴 결과와 완전히 같고, 0은 원본을 유지하며, 1을 넘으면 과장됩니다. |
| DLSS 5 | 경로 | *서명된 스니펫*: `nvngx_dlssnr.dll`을 직접 호스팅. *NGX 코어*: NGX 런타임을 통해 기능 생성. |
| 프레임 가이던스 | 모션 벡터 | NVIDIA Optical Flow(드라이버의 `nvofapi64.dll`, 양방향 일관성 검사 포함), GPU 블록 매칭, 없음(0). |
| 프레임 가이던스 | 깊이 | AI 추정(DirectML의 Depth Anything V2 Small, 갱신 간격과 네트워크 해상도 조정 가능), 평면, 그라데이션, 0. |
| 프레임 가이던스 | 자동 재설정 | 매칭 비용이 급격히 뛸 때(장면 전환) 시간 이력을 지웁니다. 기본값은 꺼짐. |
| DLAA | 사용 / 프리셋 | 뉴럴 렌더링 전 네이티브 해상도에서의 선택적 DLSS 안티앨리어싱. |
| 촬영 | 알파 유지 / 원본도 저장 / 단축키 / 타임랩스 | 촬영 옵션. |
| 표시 | 비교 / 맞춤 / VSync / 오버레이 | 미리보기 옵션. |

설정은 `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini`에 저장되며 로그는 같은 폴더의 `log.txt`입니다.

## 문제 해결

- **"Spout 송신자 대기 중"** – VRChat의 Stream 카메라에서 *Spout Stream*을 켜고 카메라를 연 상태로 두세요. 다른 Spout 송신자는 *송신자* 콤보에 표시됩니다.
- **"nvngx_dlssnr.dll을 찾을 수 없습니다"** – 런타임을 실행 파일 옆에 복사하거나 경로를 선택하세요.
- **NGX 초기화 안 됨 / DLAA 지원 안 됨** – NGX 런타임에는 NVIDIA GPU와 최신 드라이버가 필요합니다. DLSSNR은 *서명된 스니펫* 경로로 계속 동작합니다.
- **앱이 시작되지 않음 / 바로 종료됨** – `%LOCALAPPDATA%\VRChatDLSS5Cam\`를 열어 `log.txt`(마지막 줄이 실패한 단계)와 `crash.txt`(크래시 시 기록됨)를 확인하세요. 이슈에는 두 파일을 모두 첨부해 주세요.
- **뉴럴 렌더링 실패** – 일부 런타임 빌드는 더 새로운 드라이버가 필요합니다. `log.txt`의 NGX 결과 코드를 확인하고 *프리셋* 0과 *NGX 코어* 경로를 시도해 보세요.
- **깊이 추정기 사용 불가** – `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, `DirectML.dll`, `models\depth_anything_v2_small_fp16.onnx`가 실행 파일 옆에 있어야 합니다(모두 릴리스 패키지에 포함). 추정기가 준비될 때까지 앱은 0 깊이로 대체하며, 상태는 *프레임 가이던스*에 표시됩니다.
- **옵티컬 플로우 사용 불가** – NVIDIA Optical Flow는 앱 전용 네이티브 D3D11 장치에서 실행됩니다(드라이버가 D3D11On12 레이어를 거부하며, 이것이 0.2.0의 "UNSUPPORTED_DEVICE" 오류의 원인이었습니다). `log.txt`에 "NVOF unavailable, falling back to block matching"이 보이면 GeForce 드라이버를 업데이트하세요. 그때까지는 블록 매칭이 자동으로 사용됩니다. *프레임 가이던스*의 상태 점이 현재 사용 중인 소스를 보여 줍니다.
- **낮은 프레임률** – DLAA를 끄거나, 깊이 갱신 간격을 늘리거나 깊이 네트워크 해상도를 낮추거나, 검색 반경을 줄이거나, 모션 벡터에 NVIDIA Optical Flow를 선택하세요. NVIDIA Optical Flow를 사용할 때는 플로우 그리드를 4 px로 유지하세요(가장 빠른 설정. 2 px / 1 px는 4K에서 훨씬 무겁습니다). 로그에는 15초마다 `Perf:` 줄이 기록되어 처리 속도, 프레임당 CPU 비용(수신 / 대기 / 기록 / 제출), 각 단계의 GPU 시간, 깊이 네트워크 비용을 보여 줍니다(실제로 처리된 프레임만 집계). 인터페이스는 별도 스레드에서 실행되므로 처리 속도가 낮아도 창이 느려지지 않습니다.

## 소스에서 빌드

필요 사항: Visual Studio 2022(MSVC v143, Windows 10 SDK) 및 CMake 3.21 이상.

```powershell
git clone https://github.com/AlanBacker/VRChat-DLSS5-Cam.git
cd VRChat-DLSS5-Cam
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

구성 단계에서 NVIDIA의 공개 GitHub 저장소로부터 NVIDIA DLSS SDK(헤더, `nvsdk_ngx_s.lib`, `nvngx_dlss.dll`)를, NuGet에서
ONNX Runtime(DirectML 빌드)과 DirectML을, Hugging Face에서 Depth Anything V2 Small FP16 모델을 내려받습니다
(`-DVDC_FETCH_DEPTH_MODEL=OFF`로 모델을 건너뛸 수 있음). 모든 다운로드는 해시를 검증합니다.
셰이더는 실행 시 컴파일되므로 별도의 셰이더 도구 체인이 필요 없습니다.

## 동작 원리

```
VRChat Stream 카메라 ──Spout──▶ D3D11on12 수신 ──▶ 변환(sRGB / 크기 조정)
      ▶ NVIDIA Optical Flow(정방향 + 역방향) / 블록 매칭 ──▶ 모션 벡터 + 신뢰도
      ▶ Depth Anything V2(ONNX Runtime DirectML, N 프레임마다) ──▶ 정규화된 깊이, 그 사이에는 모션 벡터로 재투영
      ▶ [DLAA] ──▶ DLSSNR(nvngx_dlssnr.dll) ──▶ 합성 / 비교 ──▶ 미리보기 + PNG 촬영
```

이 앱은 NGX 런타임 바깥에서 DLSS 5 뉴럴 렌더링 스니펫을 호스팅합니다. DLL을 직접 로드하고
모듈 이름 검사를 통과시키며 `DLSSNR.*` NGX 파라미터 규약으로 D3D12 큐에서 기능을 생성하고 평가합니다.
자세한 내용은 `src/ngx/DlssnrFeature.cpp`를 참고하세요.

가이던스 방식은 동영상 입력에 맞춰 설계되었습니다. 동일 해상도 SDR 입력, 정방향과 역방향 벡터가 일치하지 않는 곳의 신뢰도를
낮추는 하드웨어 옵티컬 플로우, Depth Anything V2의 단안 깊이를 2/98 백분위수로 반전 상대 깊이로 정규화하고 추론 사이에는
모션 벡터로 이동, 프레임마다 이력을 초기화하지 않음. 모두 독립적인
MIT 구현입니다(`src/gfx/Pipeline.cpp`, `src/gfx/DepthEstimator.cpp`, `src/gfx/Shaders.cpp`).
옵티컬 플로우 엔진은 앱 전용 네이티브 D3D11 장치에서 실행되며, 프레임과 벡터는 NT 핸들 공유 텍스처로 D3D12와 주고받고 공유 펜스로 순서가 보장됩니다(`src/gfx/NvOpticalFlow.cpp`).

두 스레드가 GPU를 공유합니다. 처리 스레드는 Spout 수신기(또는 정지 이미지), 파이프라인, 전용 D3D12 큐를 소유하고, 인터페이스 스레드는 창, ImGui, 높은 우선순위의 프레젠트 큐를 소유합니다.
완성된 화면은 네 개의 디스플레이 버퍼와 큐 간 펜스 대기를 통해 전달되므로 미리보기는 항상 가장 최근에 완성된 프레임을 보여 주고 창은 뉴럴 패스를 기다리지 않습니다(`src/core/App.cpp`, `src/gfx/Device.cpp`).
정지 이미지는 WIC로 디코딩(EXIF 방향 적용)하여 한 번만 업로드한 뒤, 같은 파이프라인을 모션 0으로 시간적 네트워크가 안정될 때까지 여러 패스 통과시킵니다.

## 라이선스

MIT(`LICENSE` 참고). 서드파티 구성 요소와 NVIDIA 고지는 `THIRD_PARTY_NOTICES.md`에 있습니다.
이 프로젝트는 VRChat Inc. 또는 NVIDIA Corporation과 무관합니다. DLSS 5 런타임은 미출시 소프트웨어입니다.
사용에 따른 책임은 본인에게 있으며, 이 앱과 함께 재배포하지 마세요.
