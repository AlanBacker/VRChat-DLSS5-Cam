# サードパーティ表記

> この文書は翻訳です。英語版の `THIRD_PARTY_NOTICES.md` が正式な版で、ライセンス本文は原文の英語のまま載せています。

VRChat DLSS5 Cam 自体は MIT ライセンスで公開されています（`LICENSE` を参照）。
本アプリは VRChat Inc. および NVIDIA Corporation とは無関係であり、両社の承認や支援を受けたものではありません。

## NVIDIA DLSS SDK (NGX)

ビルドの構成ステップで、<https://github.com/NVIDIA/DLSS> から NVIDIA DLSS SDK（`nvsdk_ngx*.h`、`nvsdk_ngx_s.lib`、`nvngx_dlss.dll`）を取得します。
NVIDIA RTX SDKs License Agreement に基づいて使用しており、その全文は `build/ngx_sdk/NVIDIA_LICENSE.txt` にダウンロードされ、`NVIDIA_LICENSE.txt` として実行ファイルの隣に同梱されます。

**This software contains source code provided by NVIDIA Corporation.**
（本ソフトウェアには、NVIDIA Corporation が提供するソースコードが含まれています。）

## NVIDIA DLSS 5 ニューラルレンダリングランタイム（`nvngx_dlssnr.dll`）

リリースのアーカイブには、DLSS 5 ニューラルレンダリングランタイムのバージョン 310.8.0.0 が 2 つのビルドで入っています。`runtimes\blackwell\nvngx_dlssnr.dll` はゲームに同梱されている通常のビルド（GeForce RTX 50）、`runtimes\universal\nvngx_dlssnr.dll` は同じランタイムをコミュニティが GeForce RTX 40 / 30 / 20 向けに適合させたビルドです。
どちらも NVIDIA のソフトウェアで、NVIDIA の条件のもとで配布されており、本プロジェクトの MIT ライセンスの対象外です。本プロジェクトはこれらに対していかなる権利も主張しません。
ビルドワークフローはこれらを本リポジトリの `runtime-310.8` リリースから取得し、パッケージ化の前に SHA-256 チェックサムを検証します。ソースツリー自体にはランタイムのファイルは含まれていません。

## AMD FidelityFX SDK (FidelityFX API, `amd_fidelityfx_dx12.dll`)

FSR ホスト方式は、FidelityFX API を通じて FSR 3.1 アップスケーリングコンテキストをネイティブサイズで動かします。
FidelityFX API のヘッダー（`ffx_api.h`、`ffx_api_types.h`、`ffx_api_loader.h`、`ffx_upscale.h`、`dx12/ffx_api_dx12.h`）と、ビルド済みで署名付きの `amd_fidelityfx_dx12.dll` は、構成ステップで FidelityFX SDK v1.1.4 から取得され（`cmake/FetchFidelityFX.cmake`、SHA-256 で固定）、DLL は実行ファイルの隣に同梱されます。Radeon 版（`VRChatDLSS5Cam-win64-amd.zip`）には含まれ、GeForce 版には含まれません。

- ソース: <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>（タグ v1.1.4）
- ライセンス: MIT、Copyright (C) 2024 Advanced Micro Devices, Inc. — `licenses/FidelityFX-SDK-LICENSE.txt` として同梱

## DLSS-NR-on-AMD（同梱なし）

DLSS-NR-on-AMD（<https://github.com/danielblnc/DLSS-NR-on-AMD>）は、FSR を使うプロセスに取り付くことで、Radeon カードで DLSS 5 のニューラルパスを実行します。本アプリの FSR ホスト方式は、その取り付き先として用意されています。
これは独自の条件に従う別のプログラムです。その一部を本アプリに同梱したりリンクしたりはしておらず、本アプリにそこから派生したものもありません。
ユーザーが求めたとき（Radeon 版の「DLSS-NR-on-AMD をインストール…」ボタン）、本アプリはそのプロジェクト自身の GitHub リリースページからインストーラーをプログラムフォルダーにダウンロードして起動します。インストーラーは、実行ファイルの隣にある `nvngx_dlssnr.dll`（Radeon 版はこのために RTX 50 向けビルドをそこに入れています）を自身の重みファイルに変換し、その DLL をインストールします。

## Spout2 (SpoutDX)

`third_party/spout` は、Lynn Jarvis と Leading Edge による Spout2（<https://github.com/leadedge/Spout2>）の一部で、BSD-2-Clause ライセンスです。`third_party/spout/LICENSE` を参照してください。

## Dear ImGui

`third_party/imgui` は、Omar Cornut とコントリビューターによる Dear ImGui（<https://github.com/ocornut/imgui>）で、MIT ライセンスです。`third_party/imgui/LICENSE.txt` を参照してください。

## NVIDIA Optical Flow SDK（インターフェースヘッダー）

`third_party/nvof/nvOpticalFlowCommon.h` と `third_party/nvof/nvOpticalFlowD3D11.h` は NVIDIA Optical Flow SDK 5.0 のインターフェースヘッダーで、
Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES、MIT ライセンスです（ライセンス本文は各ファイルの先頭にあります）。
含まれているのはヘッダーだけです。ランタイムの `nvofapi64.dll` は NVIDIA ドライバーの一部であり、再配布していません。

## dlss5-bridge

`src/gfx/NvOpticalFlow.cpp` の土台になっている D3D11/D3D12 の相互運用についての知見（どのリソース形式を 2 つの API の間でどちら向きに共有できるか、そしてオプティカルフローのインターフェースにはネイティブ D3D11 デバイスが必要であること）は、dlss5-bridge（<https://github.com/jpneagle/dlss5-webcam-demo> の系譜、MIT ライセンス）から学んだものです。
コードはコピーしていません。

## ONNX Runtime

ONNX Runtime（DirectML ビルド）、Copyright (c) Microsoft Corporation、MIT ライセンス。
<https://github.com/microsoft/onnxruntime>
構成ステップで NuGet（`Microsoft.ML.OnnxRuntime.DirectML`）からダウンロードし、`onnxruntime.dll` と `onnxruntime_providers_shared.dll` として再配布しています。
ライセンス全文とサードパーティ表記は `licenses/ONNXRuntime-LICENSE.txt` と `licenses/ONNXRuntime-ThirdPartyNotices.txt` に同梱しています。

## DirectML

Microsoft DirectML 再配布可能コンポーネント（`DirectML.dll`）、Copyright (c) Microsoft Corporation。
<https://www.nuget.org/packages/Microsoft.AI.DirectML>
構成ステップで NuGet（`Microsoft.AI.DirectML`）からダウンロードし、そのライセンスの条件に従って再配布しています。このライセンスは、DirectML のバイナリをアプリケーションとともに再配布することを認めています。
ライセンスとサードパーティ表記は `licenses/DirectML-LICENSE.txt` と `licenses/DirectML-ThirdPartyNotices.txt` に同梱しています。

## Direct3D シェーダーコンパイラー（`d3dcompiler_47.dll`、Linux パッケージのみ）

Microsoft Direct3D HLSL コンパイラー、Copyright (c) Microsoft Corporation。Windows SDK の再配布可能コンポーネント（`Redist\D3D\x64\d3dcompiler_47.dll`）で、アプリケーションとともに配布することを認めている Microsoft Software License Terms for the Windows SDK に基づき、Linux パッケージに同梱しています。
本アプリは起動時にこの DLL でコンピュートシェーダーをコンパイルします。Windows には標準で組み込まれていますが、Proton 独自のものではシェーダーが正しくコンパイルされません。

## libwebp

libwebp（WebP コーデックライブラリ）、Copyright (c) 2010, Google Inc. All rights reserved.
BSD 3-Clause ライセンス。<https://chromium.googlesource.com/webm/libwebp>
構成ステップでプロジェクトの GitHub ミラーから取得し（タグ v1.6.0、SHA-256 で検証）、本アプリに静的リンクしています。WebP アニメーションのデコードと書き出し、そして Windows の WebP コーデックがないシステムでの WebP 静止画のデコードに使います。
ライセンス本文は `licenses/libwebp-COPYING.txt` に同梱しています。

## Microsoft Edge WebView2 SDK

Microsoft Edge WebView2 SDK、Copyright (C) Microsoft Corporation. All rights reserved.
BSD 3-Clause ライセンス。<https://www.nuget.org/packages/Microsoft.Web.WebView2>
構成ステップで NuGet から取得します（バージョン 1.0.4258.31、SHA-256 で検証）。そのヘッダーと静的ローダーライブラリ（`WebView2LoaderStatic.lib`）は本アプリに組み込まれており、本アプリはこれらを使って「AI に質問」パネルにドキュメントの AI Q&A を表示します。
WebView2 Runtime 自体は Windows の一部であり、同梱していません。ライセンス本文は `licenses/WebView2-LICENSE.txt` に同梱しています。

このパネルの AI Q&A は、Mintlify の AI Q&A ウィジェットです。本アプリには含まれていません。パネルが Mintlify のサーバー（`widget.mintlify.com`）からこれを読み込むのは、パネルを初めて開いたときだけです。

## Depth Anything V2 Small

Depth Anything V2 Small、Copyright (c) the Depth Anything V2 authors、Apache License 2.0。
<https://github.com/DepthAnything/Depth-Anything-V2>
ONNX FP16 のエクスポートは、Hugging Face で onnx-community 組織が公開しているものです:
<https://huggingface.co/onnx-community/depth-anything-v2-small>
構成ステップでダウンロードし、`models/depth_anything_v2_small_fp16.onnx` として再配布しています。
ライセンス本文は `licenses/DepthAnythingV2-LICENSE-Apache-2.0.txt` に同梱しています。

## Lucide アイコン

Lucide（<https://lucide.dev>）は ISC ライセンスです。Feather から派生したアイコンは、MIT ライセンスの対象でもあります。
インターフェースのラインアイコンは、本アプリに組み込まれた Lucide アイコンフォントのサブセット（`src/ui/IconFont.inc`、`tools/make_icon_font.py` で生成）から描いています。ライセンスは次のとおりです。

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

## フォント

本アプリは、ユーザーの Windows にすでにインストールされているフォント（Segoe UI、Microsoft YaHei、Yu Gothic、Malgun Gothic、Consolas、Segoe UI Symbol）で文字を表示します。
上記の Lucide アイコンのサブセットを除き、フォントファイルは再配布していません。
