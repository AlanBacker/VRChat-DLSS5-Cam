---
status: translated
title: コマンドライン
nav: コマンドライン
description: ファイルを開く、ウィンドウなしで処理する、テスト実行をスクリプトにする。ターミナル派の方向けです。
---
コマンドラインを使う必要はありません。ダブルクリックすれば、いつものウィンドウが開きます。コマンドラインは、スクリプト、定期実行、テストのためのものです。

```
VRChatDLSS5Cam.exe [file] [options]
```

ファイル名だけを渡すと、その画像や動画が開きます。エクスプローラーの **プログラムから開く** と同じ動作です。

## 例 {#examples}

2 つのファイルをウィンドウなしで `D:\out` に処理し、動画を HEVC で保存します。

```
VRChatDLSS5Cam.exe --headless --add "D:\shots\a.png" --add "D:\shots\clip.mp4" --set videoMatchSource=0 --set videoOutput=1 --process "D:\out"
```

動画の 1:00 から 1:10 までの 10 秒を処理し、撮影フォルダーに保存します。

```
VRChatDLSS5Cam.exe --headless --open "D:\clip.mp4" --in 1:00 --out 1:10 --process
```

動画の 30 秒の位置を表示したウィンドウを、日本語・ライトテーマで撮影してから終了します。

```
VRChatDLSS5Cam.exe --window 1600x900 --lang ja --set theme=2 --open "D:\clip.mp4" --seek 30 --screenshot 6 "D:\ui.png" --exit-after 8
```

## オプション {#options}

| オプション | 役割 |
|---|---|
| `--open <file>` {#open} | 画像や動画を開き、ライブラリに追加します。アニメーション GIF、APNG、WebP は動画として扱います。 |
| `--add <file>` {#add} | ファイルを開かずにライブラリに追加します。繰り返し指定できます。 |
| `--seek <time>` {#seek} | 動画：この時刻のフレームを表示します。`m:ss`、`h:mm:ss`、秒数で指定できます。 |
| `--play` {#play} | 動画：再生を始めます。 |
| `--in <time>` · `--out <time>` {#in-out} | 動画：処理する範囲です。音声も同じ範囲になります。 |
| `--process [folder]` {#process} | 現在の設定でライブラリ（または開いているファイル）を処理し、終了します。保存先は `folder`、指定がなければ撮影フォルダーです。 |
| `--set <key>=<value>` {#set} | `settings.ini` での名前を指定して、設定を 1 つ、今回の実行に限って変えます。下を参照してください。 |
| `--lang <en\|zh\|ja\|ko\|auto>` {#lang} | 今回の実行でのインターフェースの言語です。 |
| `--window <W>x<H>` {#window} | このウィンドウサイズで起動します。 |
| `--screenshot <seconds> <file.png>` {#screenshot} | 起動からこの秒数が経ったときに、ウィンドウの画像を保存します。繰り返し指定できます。 |
| `--exit-after <seconds>` {#exit-after} | スクリーンショットと保存が書き込まれたうえで、この秒数のあとに終了します。 |
| `--headless` {#headless} | ウィンドウを開きません。保存とスクリーンショットは通常どおり動き、処理が終わるとアプリは終了します。 |
| `--data-dir <folder>` {#data-dir} | 設定、プリセット、ログを `%LOCALAPPDATA%\VRChatDLSS5Cam` ではなく、このフォルダーに置きます。 |
| `--mcp` {#mcp} | MCP クライアント用のブリッジです。[MCP](mcp.html) |
| `--mcp-port <port>` {#mcp-port} | 今回のセッションでは、このポートで MCP サーバーを動かします。 |
| `--update` {#update} | 選んでいるチャネルで新しいバージョンを探し、インストールします。 |
| `--edition <geforce\|amd>` {#edition} | アップデートと同じ方法で、もう一方の版に切り替えます。 |

## 1 回だけの設定 {#set-keys}

`--set` には `settings.ini` で使われている名前を指定します。コマンドラインで指定した値は保存されません。便利なものをいくつか挙げます。

| キー | 値 |
|---|---|
| `nrIntensity` | `0`〜`2`。たとえば `--set nrIntensity=1.5`。 |
| `videoMatchSource` | `1`（既定）は元の動画に合わせます。`0` では下の 3 つのキーを使います。 |
| `videoOutput` | `0` MP4 H.264、`1` MP4 HEVC、`2` PNG 連番、`3` GIF、`4` APNG、`5` WebP。 |
| `videoBitrate` · `webpQuality` | MP4 のビットレートと、WebP の品質（`50`〜`100`）です。 |
| `keepAudio` | `0` で音声を外します。 |
| `outputName` · `captureName` | ファイル名のテンプレートです。[使える言葉](saving.html#names) |
| `customResolution` · `customWidth` · `upscaleMode` | アップスケール：`--set customResolution=1 --set customWidth=3840 --set upscaleMode=0`（`0` は DLSS 超解像、`1` はリサンプリング）。 |
| `theme` | `0` は Windows に合わせる、`1` はダーク、`2` はライト。 |
| `updateCheck` · `updateChannel` | 起動時の確認（`1` でオン）と、チャネル（`0` は安定版、`1` はプレリリース版）です。 |
| `driverCheck` | 起動時のドライバーの通知（`1` でオン）です。 |
| `reopenLast` | 起動時に前回のファイルを開くかどうか（`0` でオフ）です。 |

## 終了コード {#exit-codes}

| コード | 意味 |
|---|---|
| `0` | すべて成功しました。 |
| `1` | 処理に失敗したファイルがあります。 |
| `2` | アプリがクラッシュした（`crash.txt` が書き出されます）か、グラフィックデバイスが失われました。 |

ログ `log.txt` にはコマンドラインからの操作がすべて記録されるので、失敗した実行をあとから確認できます。Linux では、`vrchat-dlss5-cam` ランチャーが同じオプションを受け付けます。[Linux](linux.html)

テスト用のオプションを含む全一覧は、GitHub の [COMMAND_LINE.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/COMMAND_LINE.md) にあります。
