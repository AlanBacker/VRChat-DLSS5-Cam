<p align="center">
  <img src="../resources/app-256.png" width="96" alt="VRChat DLSS5 Cam アイコン">
</p>

# VRChat DLSS5 Cam

[English](../README.md) · [简体中文](README.zh-CN.md) · **日本語** · [한국어](README.ko.md)

VRChat DLSS5 Cam は、VRChat 内蔵カメラ（*Spout Stream* を有効にした Stream カメラ）の映像を取り込み、
**DLSS 5 ニューラルレンダリング（DLSSNR）** でリアルタイム処理し、その結果をライブ表示しながら
ロスレス PNG 写真として保存します。単体の Windows アプリであり、VRChat への注入や Mod は不要です。

> **ステータス: 早期プレビュー。** バイナリは GitHub Actions でビルドされており、パイプラインはまだ幅広い GPU や
> DLSS 5 ランタイムのビルドで検証されていません。不具合があれば `log.txt` を添えて Issue を作成してください。

## 機能

- DLSS 5 処理後の VRChat カメラ映像を**ライブプレビュー**。ワイプ比較、オリジナル、モーションベクトル表示に対応。
- `nvngx_dlssnr.dll` を直接ホストする **DLSS 5 ニューラルレンダリング**。RenoDX の DLSS 5 ReShade アドオンと同じパラメータ
  （プリセット、スタイル、強度、グローバルトーン、ローカルトーン、ローカル構造、肌の構造、自動マスク、UI 補正）を調整可能。
  変更は即座に反映され、静止したフレーム上でも調整できます。
- **フレームガイダンス。** DLSSNR は時間方向のモデルで、モーションベクトルと深度を必要とします。本アプリは GPU 上で
  （階層ブロックマッチングにより）これらを計算し、利用可能なら **NVIDIA Optical Flow** ハードウェアエンジンを使用、
  さらにニューラルレンダリングの前に **DLAA**（ネイティブ解像度の DLSS アンチエイリアス）パスを追加できます。
- **適応解像度。** 送信元の解像度を自動検出。カスタム出力解像度を設定でき、DLSS 5 にアップスケールさせることも可能。
  シーンカットを検出して時間履歴をリセットします。
- **ロスレス撮影。** 処理後のフレーム（任意でオリジナルも）を PNG で保存。VRChat が前面でも動作するグローバルホットキー
  （既定 `Ctrl+Alt+P`）と、タイムラプスモードを搭載。
- **4 言語対応**（English、简体中文、日本語、한국어）。Windows の UI 言語から自動選択。
- モニターごとの DPI 対応、ダークテーマ UI、GPU タイマー、内蔵ログ。

## 動作要件

| | |
|---|---|
| OS | Windows 10 21H2 / Windows 11（64 ビット） |
| GPU | NVIDIA GeForce RTX（DLSS 5 ニューラルレンダリングは RTX ハードウェア専用）、最新の Game Ready ドライバー |
| VRChat | Stream カメラに *Spout Stream* オプションがあるビルド（デスクトップ / VR） |
| DLSS 5 ランタイム | ご自身で用意した `nvngx_dlssnr.dll`。**本プロジェクトには含まれず、ダウンロードもしません。** |

## セットアップ

1. [Releases](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases) から `VRChatDLSS5Cam-win64.zip` をダウンロードして任意の場所に展開します。
2. `nvngx_dlssnr.dll` を展開したフォルダ（`VRChatDLSS5Cam.exe` と同じ場所）にコピーします。*DLSS 5 ニューラルレンダリング → ランタイムのパス* からファイルを指定することもできます。
3. VRChat を起動して**カメラ**を開き、カメラモードを **Stream** に切り替え、Stream カメラ設定で **Spout Stream** を有効にします。VRChat はカメラ映像を Spout 送信元（`VRCSender1`）として公開します。
4. `VRChatDLSS5Cam.exe` を起動します。送信元は自動的に検出され、処理後の映像がプレビューに表示されます。
5. VRChat で構図を決めて **Ctrl+Alt+P**（または *撮影* ボタン）を押します。PNG は既定で `ピクチャ\VRChat DLSS5 Cam` に保存されます。

ヒント
- Spout ストリームの解像度は VRChat 側で決まります。VRChat の `config.json` で `camera_spout_res_width` / `camera_spout_res_height` を上げると高解像度の入力が得られ、アプリは自動的に追従します。
- 別の出力サイズにしたい場合は *ソース* セクションの *カスタム解像度* を有効にします。*DLSS 5 アップスケール* をオンにすると DLSSNR が大きな画像を直接描画します。
- *ワイプ* 比較モードでプレビュー内のハンドルをドラッグすると、各パラメータの効果を確認できます。

## パラメータ

| セクション | 設定 | 意味 |
|---|---|---|
| DLSS 5 | プリセット | DLSSNR に渡すレンダープリセットのヒント（0–3）。 |
| DLSS 5 | スタイル | `DLSSNR.Style`: 既定 / ナチュラル / シネマティック。 |
| DLSS 5 | 強度 | ニューラルパス全体の強さ。 |
| DLSS 5 | グローバルトーン / ローカルトーン | グローバル・ローカルのトーンマッピング強度。 |
| DLSS 5 | ローカル構造 / 肌の構造 | ディテール強調。肌の構造はランタイム既定のままにできます。 |
| DLSS 5 | 自動マスク / UI 補正 | 自動被写体マスク、UI セーフ処理。 |
| DLSS 5 | ルート | *署名スニペット*: `nvngx_dlssnr.dll` を直接ホスト。*NGX コア*: NGX ランタイム経由で機能を作成。 |
| フレームガイダンス | モーションベクトル | なし（ゼロ）、GPU ブロックマッチング、NVIDIA Optical Flow（ドライバーの `nvofapi64.dll`）。 |
| フレームガイダンス | 深度 | DLSSNR に渡す深度バッファ: フラット / グラデーション / ゼロ。 |
| フレームガイダンス | 自動リセット | ブロックマッチングのコストからシーンカットを検出し時間履歴をクリア。 |
| DLAA | 有効 / プリセット | ニューラルレンダリング前のネイティブ解像度 DLSS アンチエイリアス（任意）。 |
| 撮影 | アルファ保持 / オリジナルも保存 / ホットキー / タイムラプス | 撮影オプション。 |
| 表示 | 比較 / フィット / VSync / オーバーレイ | プレビューオプション。 |

設定は `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini` に保存され、ログは同じフォルダの `log.txt` です。

## トラブルシューティング

- **「Spout 送信元を待機中」** – VRChat の Stream カメラで *Spout Stream* を有効にし、カメラを開いたままにしてください。他の Spout 送信元は *送信元* コンボに表示されます。
- **「nvngx_dlssnr.dll が見つかりません」** – ランタイムを実行ファイルの隣にコピーするか、パスを選択してください。
- **NGX 未初期化 / DLAA 非対応** – NGX ランタイムには NVIDIA GPU と最新ドライバーが必要です。DLSSNR は *署名スニペット* ルートで引き続き動作します。
- **起動しない / すぐに終了する** – `%LOCALAPPDATA%\VRChatDLSS5Cam\` を開き、`log.txt`（最後の行が失敗したステップ）と `crash.txt`（クラッシュ時に書き込まれます）を確認してください。Issue には両方を添付してください。
- **ニューラルレンダリングに失敗** – 一部のランタイムビルドは新しいドライバーを必要とします。`log.txt` の NGX 結果コードを確認し、*プリセット* 0 や *NGX コア* ルートを試してください。
- **フレームレートが低い** – DLAA を無効にする、探索半径を下げる、モーションベクトルに NVIDIA Optical Flow を選ぶ。

## ソースからのビルド

必要なもの: Visual Studio 2022（MSVC v143、Windows 10 SDK）と CMake 3.21 以降。

```powershell
git clone https://github.com/AlanBacker/VRChat-DLSS5-Cam.git
cd VRChat-DLSS5-Cam
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

構成ステップで NVIDIA の公開 GitHub リポジトリから NVIDIA DLSS SDK（ヘッダー、`nvsdk_ngx_s.lib`、`nvngx_dlss.dll`）をダウンロードします。
シェーダーは実行時にコンパイルされるため、シェーダーツールチェーンは不要です。

## 仕組み

```
VRChat Stream カメラ ──Spout──▶ D3D11on12 受信 ──▶ 変換（sRGB / リサイズ）
      ▶ 輝度ピラミッド ──▶ ブロックマッチング / NVIDIA Optical Flow ──▶ モーションベクトル + 深度
      ▶ [DLAA] ──▶ DLSSNR（nvngx_dlssnr.dll）──▶ 合成 / 比較 ──▶ プレビュー + PNG 撮影
```

本アプリは NGX ランタイムの外側で DLSS 5 ニューラルレンダリングスニペットをホストします。
DLL を直接読み込み、モジュール名チェックを満たし、`DLSSNR.*` NGX パラメータ規約を使って D3D12 コンピュートキュー上で
機能を作成・評価します。詳細は `src/ngx/DlssnrFeature.cpp` を参照してください。

## ライセンス

MIT（`LICENSE` 参照）。サードパーティコンポーネントと NVIDIA の表記は `THIRD_PARTY_NOTICES.md` に記載しています。
本プロジェクトは VRChat Inc. および NVIDIA Corporation とは無関係です。DLSS 5 ランタイムは未リリースのソフトウェアです。
自己責任で使用し、本アプリと一緒に再配布しないでください。
