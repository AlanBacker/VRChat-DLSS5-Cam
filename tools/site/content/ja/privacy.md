---
status: translated
title: プライバシーとライセンス
nav: プライバシーとライセンス
description: アプリがネットワークに送るもの（ほとんどありません）と、アプリとその部品のライセンスです。
---
## アップロードされるものはありません {#nothing-uploaded}

画像、動画、設定は、お使いの PC から出ることはありません。アプリにはアカウントも、トラッキングも、利用統計もありません。処理はすべて、お使いのグラフィックカード上で行われます。

## アプリがネットに接続するとき {#network}

アプリがインターネットに接続するのは、次の場合だけです。

| 内容 | タイミング | 接続先 |
|---|---|---|
| 更新の確認 {#update-check} | **起動時に更新を確認** がオフでない限り、起動のたび。および **更新を確認** をクリックしたとき。 | GitHub、または **GitHub へのアクセス** で選んだミラーサイト。 |
| 更新のダウンロード {#update-download} | **今すぐ更新** をクリックしたとき、または `--update` や `--edition` を実行したときだけ。 | 同上。 |
| ミラーサイトの測定 {#mirror-sites} | **ミラーサイト（最速を自動選択）** を選んでいるとき、または **サイトを測定** をクリックしたとき。 | 既知の 8 つのミラーサイト。 |
| DLSS-NR-on-AMD {#port} | Radeon 版のみ：最新リリースの確認と、ボタンをクリックしたときのインストーラーのダウンロード。 | GitHub 上のそのプロジェクトのリリースページ、または選んだミラーサイト。 |

コマンドラインから `--headless` や `--process` を付けて実行した場合は、自分から更新を確認することはありません。

**ドキュメント**、**ドライバーをダウンロード**、**問題を報告** などのボタンは、ブラウザーでページを開きます。**問題を報告** は GitHub のフォームにアプリのバージョンとお使いのグラフィックカードを記入します。送信するまで、何も送られることはありません。

## MCP サーバー {#mcp}

MCP サーバーは、オンにするまで動きません。オンにしても待ち受けるのはお使いの PC だけで、自分からどこかに接続することはありません。別の PC から接続できるのは、**到達範囲** を **ローカルネットワーク** にし、その PC に鍵を渡したあとだけです。[MCP](mcp.html)

## このサイト {#this-site}

<!-- if askWidget -->
このサイトのページが、自分からほかのサーバーのものを読み込むことはありません。フォントも、スクリプトも、トラッキングもありません。例外は [AI Q&A](#ai-qa) だけで、それも使おうとしたときに限ります。選んだテーマと言語は、お使いのブラウザーの中にだけ保存されます。

## AI Q&A {#ai-qa}

**AI に質問** で AI Q&A が開きます。このサイトのページをもとに質問に答える機能で、提供しているのはこのサイトではなく [Mintlify](https://mintlify.com) です。

ページを開いても、Mintlify のものは何も読み込まれません。トップバーや検索結果の最後にある **AI に質問** にポインターを合わせるか、キーボードでそこへ移動したときに読み込みが始まり、クリックしたときにはすぐ使えるようになっています。入力した内容は Mintlify に送られ、Mintlify が回答を書くので、個人的な情報は入れないでください。Mintlify は、パネルが開いたときなどのパネルの利用イベントも受け取ります。このサイトは、そこにページのアドレスを含めないよう Mintlify に求めています。ボットを防ぐため、Mintlify は hCaptcha で質問を確認します。このチェックは、パネルが hCaptcha のサーバーから読み込みます。このブラウザーのタブの中では、パネルが会話への参照を保持するので、続けて質問すると同じ会話の続きになります。
<!-- else -->
このサイトのページは、ほかのサーバーから何も読み込みません。フォントも、スクリプトも、トラッキングもありません。選んだテーマと言語は、お使いのブラウザーの中にだけ保存されます。
<!-- endif -->

## ライセンス {#licence}

アプリのライセンスの記載です（README のとおり）。

> MIT（`LICENSE` を参照）。サードパーティコンポーネントと NVIDIA の表記は `THIRD_PARTY_NOTICES.md` に記載しています。本プロジェクトは VRChat Inc. および NVIDIA Corporation とは無関係です。

全文は [LICENSE](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/LICENSE) と [THIRD_PARTY_NOTICES.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/THIRD_PARTY_NOTICES.md) にあります。どちらもアプリのフォルダーにも入っていて、**情報** → **サードパーティ表記** で開けます。

## アプリが使っている部品 {#components}

| 部品 | ライセンス |
|---|---|
| NVIDIA DLSS 5 ランタイム（`nvngx_dlssnr.dll`） | NVIDIA の利用条件に基づく NVIDIA のソフトウェアで、アプリの MIT ライセンスの対象外です。GeForce 版には 2 つのビルド、Radeon 版には DLSS-NR-on-AMD のインストーラー用に 1 つが入っています。 |
| NVIDIA DLSS SDK（NGX、`nvngx_dlss.dll`） | NVIDIA RTX SDKs License Agreement。 |
| NVIDIA Optical Flow SDK（インターフェースのヘッダー） | MIT。 |
| AMD FidelityFX SDK（`amd_fidelityfx_dx12.dll`、Radeon 版） | MIT。 |
| Spout2 | BSD 2-Clause。 |
| Dear ImGui | MIT。 |
| dlss5-bridge | MIT。 |
| ONNX Runtime | MIT。 |
| DirectML | Microsoft の DirectML ライセンス。アプリと一緒に配布することが認められています。 |
| Depth Anything V2 Small | Apache License 2.0。 |
| libwebp | BSD 3-Clause。 |
| Lucide アイコン | ISC。Feather から派生したアイコンは MIT も。 |
| Direct3D シェーダーコンパイラー（`d3dcompiler_47.dll`、Linux パッケージ） | Windows SDK の Microsoft ソフトウェア ライセンス条項。 |

**DLSS-NR-on-AMD** は含まれていません。独自の利用条件を持つ別のプロジェクトで、その条件は個人的・非商用の利用を認め、再配布を認めていません。Radeon 版は、求められたときだけ、そのプロジェクトのリリースページからインストーラーをダウンロードします。

このサイトは ISC ライセンスのもとで Lucide アイコンを使っています。ライセンス文は [assets/licenses/lucide-LICENSE.txt]({root}assets/licenses/lucide-LICENSE.txt) にあります。
