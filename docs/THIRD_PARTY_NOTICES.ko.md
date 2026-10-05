# 서드파티 고지

> 이 문서는 번역본입니다. 영어판 `THIRD_PARTY_NOTICES.md`가 정본이며, 라이선스 본문은 영어 원문 그대로 두었습니다.

VRChat DLSS5 Cam 자체는 MIT 라이선스로 배포됩니다(`LICENSE` 참고).
이 앱은 VRChat Inc. 및 NVIDIA Corporation과 무관하며, 두 회사의 승인이나 지원을 받지 않습니다.

## NVIDIA DLSS SDK (NGX)

빌드는 구성 단계에서 NVIDIA DLSS SDK(`nvsdk_ngx*.h`, `nvsdk_ngx_s.lib`, `nvngx_dlss.dll`)를 <https://github.com/NVIDIA/DLSS>에서 내려받습니다.
NVIDIA RTX SDKs License Agreement에 따라 사용하며, 전문은 `build/ngx_sdk/NVIDIA_LICENSE.txt`로 내려받아 실행 파일 옆에 `NVIDIA_LICENSE.txt`로 함께 배포합니다.

**This software contains source code provided by NVIDIA Corporation.**
(이 소프트웨어에는 NVIDIA Corporation이 제공한 소스 코드가 포함되어 있습니다.)

## NVIDIA DLSS 5 뉴럴 렌더링 런타임(`nvngx_dlssnr.dll`)

릴리스 압축 파일에는 DLSS 5 뉴럴 렌더링 런타임 310.8.0.0 버전이 두 가지 빌드로 들어 있습니다. `runtimes\blackwell\nvngx_dlssnr.dll`은 게임에 실리는 그대로의 빌드(GeForce RTX 50)이고, `runtimes\universal\nvngx_dlssnr.dll`은 같은 런타임을 커뮤니티가 GeForce RTX 40 / 30 / 20용으로 적합화한 빌드입니다.
둘 다 NVIDIA의 조건에 따라 배포되는 NVIDIA의 소프트웨어로 이 프로젝트의 MIT 라이선스 대상이 아니며, 이 프로젝트는 이에 대해 어떠한 권리도 주장하지 않습니다.
빌드 워크플로는 이 저장소의 `runtime-310.8` 릴리스에서 두 파일을 받아 패키징 전에 SHA-256 체크섬을 검증합니다. 소스 트리 자체에는 런타임 파일이 들어 있지 않습니다.

## AMD FidelityFX SDK (FidelityFX API, `amd_fidelityfx_dx12.dll`)

FSR 호스트 방식은 FidelityFX API를 통해 FSR 3.1 업스케일링 컨텍스트를 네이티브 크기로 실행합니다.
FidelityFX API의 헤더(`ffx_api.h`, `ffx_api_types.h`, `ffx_api_loader.h`, `ffx_upscale.h`, `dx12/ffx_api_dx12.h`)와 미리 빌드되어 서명된 `amd_fidelityfx_dx12.dll`은 구성 단계에서 FidelityFX SDK v1.1.4로부터 받아 오며(`cmake/FetchFidelityFX.cmake`, SHA-256 고정), DLL은 실행 파일 옆에 함께 배포됩니다. Radeon 에디션(`VRChatDLSS5Cam-win64-amd.zip`)에는 들어 있고 GeForce 에디션에는 없습니다.

- 소스: <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK> (태그 v1.1.4)
- 라이선스: MIT, Copyright (C) 2024 Advanced Micro Devices, Inc. — `licenses/FidelityFX-SDK-LICENSE.txt`로 함께 배포

## DLSS-NR-on-AMD(포함되지 않음)

DLSS-NR-on-AMD(<https://github.com/danielblnc/DLSS-NR-on-AMD>)는 FSR을 쓰는 프로세스에 붙어 Radeon 카드에서 DLSS 5 뉴럴 패스를 실행합니다. 이 앱의 FSR 호스트 방식은 DLSS-NR-on-AMD가 붙을 수 있도록 마련된 것입니다.
DLSS-NR-on-AMD는 자체 약관을 따르는 별도의 프로그램으로, 그 어떤 부분도 이 앱에 포함되거나 링크되어 있지 않으며 이 앱의 어떤 부분도 그것에서 파생되지 않았습니다.
사용자가 요청하면(Radeon 에디션의 "DLSS-NR-on-AMD 설치…" 버튼) 앱은 그 프로젝트 자체의 GitHub 릴리스 페이지에서 설치 프로그램을 프로그램 폴더에 내려받아 실행합니다. 설치 프로그램은 실행 파일 옆의 `nvngx_dlssnr.dll`(Radeon 에디션은 이를 위해 그 자리에 RTX 50용 빌드를 넣어 둡니다)을 자체 가중치 파일로 변환하고 자신의 DLL을 설치합니다.

## Spout2 (SpoutDX)

`third_party/spout`는 Lynn Jarvis와 Leading Edge가 만든 Spout2(<https://github.com/leadedge/Spout2>)의 일부로, BSD-2-Clause 라이선스입니다. `third_party/spout/LICENSE`를 참고하세요.

## Dear ImGui

`third_party/imgui`는 Omar Cornut와 기여자들이 만든 Dear ImGui(<https://github.com/ocornut/imgui>)로, MIT 라이선스입니다. `third_party/imgui/LICENSE.txt`를 참고하세요.

## NVIDIA Optical Flow SDK(인터페이스 헤더)

`third_party/nvof/nvOpticalFlowCommon.h`와 `third_party/nvof/nvOpticalFlowD3D11.h`는 NVIDIA Optical Flow SDK 5.0의 인터페이스 헤더로,
Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES, MIT 라이선스입니다(라이선스 본문은 각 파일 맨 위에 있습니다).
헤더만 포함되어 있으며, 런타임인 `nvofapi64.dll`은 NVIDIA 드라이버의 일부로 재배포하지 않습니다.

## dlss5-bridge

`src/gfx/NvOpticalFlow.cpp`의 바탕이 된 D3D11/D3D12 상호 운용 지식(어떤 리소스 형식을 두 API 사이에서 어느 방향으로 공유할 수 있는지, 그리고 옵티컬 플로우 인터페이스에는 네이티브 D3D11 장치가 필요하다는 점)은 dlss5-bridge(<https://github.com/jpneagle/dlss5-webcam-demo> 계열, MIT 라이선스)에서 배운 것입니다.
코드는 복사하지 않았습니다.

## ONNX Runtime

ONNX Runtime(DirectML 빌드), Copyright (c) Microsoft Corporation, MIT 라이선스.
<https://github.com/microsoft/onnxruntime>
구성 단계에서 NuGet(`Microsoft.ML.OnnxRuntime.DirectML`)으로부터 내려받아 `onnxruntime.dll`과 `onnxruntime_providers_shared.dll`로 재배포합니다.
라이선스 전문과 서드파티 고지는 `licenses/ONNXRuntime-LICENSE.txt`와 `licenses/ONNXRuntime-ThirdPartyNotices.txt`로 함께 배포됩니다.

## DirectML

Microsoft DirectML 재배포 가능 구성 요소(`DirectML.dll`), Copyright (c) Microsoft Corporation.
<https://www.nuget.org/packages/Microsoft.AI.DirectML>
구성 단계에서 NuGet(`Microsoft.AI.DirectML`)으로부터 내려받아 해당 라이선스의 조건에 따라 재배포합니다. 이 라이선스는 DirectML 바이너리를 애플리케이션과 함께 재배포하는 것을 허용합니다.
라이선스와 서드파티 고지는 `licenses/DirectML-LICENSE.txt`와 `licenses/DirectML-ThirdPartyNotices.txt`로 함께 배포됩니다.

## Direct3D 셰이더 컴파일러(`d3dcompiler_47.dll`, Linux 패키지 전용)

Microsoft Direct3D HLSL 컴파일러, Copyright (c) Microsoft Corporation. Windows SDK의 재배포 가능 구성 요소(`Redist\D3D\x64\d3dcompiler_47.dll`)로, 애플리케이션과 함께 배포하는 것을 허용하는 Microsoft Software License Terms for the Windows SDK에 따라 Linux 패키지에 포함되어 있습니다.
앱은 시작할 때 이 DLL로 컴퓨트 셰이더를 컴파일합니다. Windows에는 기본으로 들어 있으며, Proton 자체의 버전은 셰이더를 올바르게 컴파일하지 못합니다.

## libwebp

libwebp(WebP 코덱 라이브러리), Copyright (c) 2010, Google Inc. All rights reserved.
BSD 3-Clause 라이선스. <https://chromium.googlesource.com/webm/libwebp>
구성 단계에서 프로젝트의 GitHub 미러로부터 받아(태그 v1.6.0, SHA-256 검증) 앱에 정적으로 링크합니다. WebP 애니메이션 파일을 디코딩하고 저장하며, Windows의 WebP 코덱이 없는 시스템에서는 WebP 정지 화상도 디코딩합니다.
라이선스 본문은 `licenses/libwebp-COPYING.txt`로 함께 배포됩니다.

## Microsoft Edge WebView2 SDK

Microsoft Edge WebView2 SDK, Copyright (C) Microsoft Corporation. All rights reserved.
BSD 3-Clause 라이선스. <https://www.nuget.org/packages/Microsoft.Web.WebView2>
구성 단계에서 NuGet으로부터 내려받습니다(버전 1.0.4258.31, SHA-256 검증). 헤더와 정적 로더 라이브러리(`WebView2LoaderStatic.lib`)는 앱에 컴파일되어 들어가며, 앱은 이를 이용해 "AI에게 묻기" 패널에 문서의 AI Q&A를 표시합니다.
WebView2 Runtime 자체는 Windows의 일부이며 함께 배포하지 않습니다. 라이선스 본문은 `licenses/WebView2-LICENSE.txt`로 함께 배포됩니다.

이 패널의 AI Q&A는 Mintlify의 AI Q&A 위젯입니다. 앱에는 포함되어 있지 않으며, 패널은 처음 열릴 때에만 Mintlify의 서버(`widget.mintlify.com`)에서 위젯을 불러옵니다.

## Depth Anything V2 Small

Depth Anything V2 Small, Copyright (c) the Depth Anything V2 authors, Apache License 2.0.
<https://github.com/DepthAnything/Depth-Anything-V2>
Hugging Face에서 onnx-community 조직이 공개한 ONNX FP16 내보내기:
<https://huggingface.co/onnx-community/depth-anything-v2-small>
구성 단계에서 내려받아 `models/depth_anything_v2_small_fp16.onnx`로 재배포합니다.
라이선스 본문은 `licenses/DepthAnythingV2-LICENSE-Apache-2.0.txt`로 함께 배포됩니다.

## Lucide 아이콘

Lucide(<https://lucide.dev>)는 ISC 라이선스이며, Feather에서 파생된 아이콘에는 MIT 라이선스도 적용됩니다.
인터페이스의 라인 아이콘은 앱에 컴파일되어 들어간 Lucide 아이콘 글꼴의 서브셋(`src/ui/IconFont.inc`, `tools/make_icon_font.py`로 생성)에서 그립니다. 라이선스는 다음과 같습니다.

```
ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

The following Lucide icons are derived from the Feather project:

airplay, alert-circle, alert-octagon, alert-triangle, aperture, arrow-down-circle, arrow-down-left, arrow-down-right, arrow-down, arrow-left-circle, arrow-left, arrow-right-circle, arrow-right, arrow-up-circle, arrow-up-left, arrow-up-right, arrow-up, at-sign, calendar, cast, check, chevron-down, chevron-left, chevron-right, chevron-up, chevrons-down, chevrons-left, chevrons-right, chevrons-up, circle, clipboard, clock, code, columns, command, compass, corner-down-left, corner-down-right, corner-left-down, corner-left-up, corner-right-down, corner-right-up, corner-up-left, corner-up-right, crosshair, database, divide-circle, divide-square, dollar-sign, download, external-link, feather, frown, hash, headphones, help-circle, info, italic, key, layout, life-buoy, link-2, link, loader, lock, log-in, log-out, maximize, meh, minimize, minimize-2, minus-circle, minus-square, minus, monitor, moon, more-horizontal, more-vertical, move, music, navigation-2, navigation, octagon, pause-circle, percent, plus-circle, plus-square, plus, power, radio, rss, search, server, share, shopping-bag, sidebar, smartphone, smile, square, table-2, tablet, target, terminal, trash-2, trash, triangle, tv, type, upload, x-circle, x-octagon, x-square, x, zoom-in, zoom-out

The MIT License (MIT) (for the icons listed above)

Copyright (c) 2013-present Cole Bemis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 글꼴

앱은 사용자의 Windows에 이미 설치되어 있는 글꼴(Segoe UI, Microsoft YaHei, Yu Gothic, Malgun Gothic, Consolas, Segoe UI Symbol)로 텍스트를 표시합니다.
위의 Lucide 아이콘 서브셋을 제외하면 글꼴 파일은 재배포하지 않습니다.
