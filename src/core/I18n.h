// VRChat DLSS5 Cam - user interface strings in English, Chinese, Japanese and Korean.
#pragma once

namespace vdc {

enum class Lang { English = 0, Chinese = 1, Japanese = 2, Korean = 3, Count = 4 };

// X-macro keeps the identifier list and the translation table in sync.
#define VDC_STRING_LIST(X) \
    X(AppTitle,           "VRChat DLSS5 Cam", "VRChat DLSS5 Cam", "VRChat DLSS5 Cam", "VRChat DLSS5 Cam") \
    X(CompareMotion,      "Motion vectors", "运动矢量", "モーションベクトル", "모션 벡터") \
    X(Reload,             "Reload", "重新加载", "再読み込み", "다시 불러오기") \
    X(ResetView,          "Reset view", "重置视图", "表示をリセット", "보기 초기화") \
    X(Zoom,               "Zoom", "缩放", "ズーム", "확대") \
    X(TipZoom,            "Magnification of the preview relative to the picture's pixels. The mouse wheel over the preview zooms around the cursor, dragging pans, a double-click returns to the fitted view.", "预览相对于图片像素的放大倍率。在预览上滚动滚轮可围绕光标缩放，拖动可平移，双击恢复适应窗口。", "プレビューの画像ピクセルに対する拡大率。プレビュー上でホイールを回すとカーソル位置を中心に拡大縮小、ドラッグで移動、ダブルクリックでウィンドウに合わせた表示に戻ります。", "미리보기의 그림 픽셀 기준 확대 배율. 미리보기 위에서 휠을 굴리면 커서를 중심으로 확대/축소, 드래그로 이동, 더블클릭하면 창 맞춤으로 돌아갑니다.") \
    X(Timers,             "GPU timings", "GPU 耗时", "GPU 処理時間", "GPU 처리 시간") \
    X(TmConvert,          "Convert", "转换", "変換", "변환") \
    X(TmGuidance,         "Guidance", "引导", "ガイダンス", "가이던스") \
    X(TmOpticalFlow,      "Optical flow", "光流", "オプティカルフロー", "옵티컬 플로우") \
    X(TmDlaa,             "DLAA", "DLAA", "DLAA", "DLAA") \
    X(TmNeural,           "Neural", "神经渲染", "ニューラル", "뉴럴") \
    X(TmComposite,        "Composite", "合成", "合成", "합성") \
    X(TmUi,               "UI", "界面", "UI", "UI") \
    X(TipUpscale,         "Let DLSS 5 upscale from the sender resolution to the custom resolution instead of resampling first (experimental).", "让 DLSS 5 直接从发送端分辨率放大到自定义分辨率，而不是先重采样（实验性）。", "先にリサンプリングせず、送信側の解像度からカスタム解像度へ DLSS 5 でアップスケールします（実験的）。", "먼저 리샘플링하지 않고 DLSS 5가 송신 해상도에서 사용자 지정 해상도로 업스케일하도록 합니다(실험적).") \
    X(TipRoute,           "Signed snippet: talk to nvngx_dlssnr.dll directly (recommended). NGX core: ask the NVIDIA NGX runtime to create the feature.", "签名片段：直接调用 nvngx_dlssnr.dll（推荐）。NGX 核心：通过 NVIDIA NGX 运行时创建功能。", "Signed snippet：nvngx_dlssnr.dll を直接呼び出します（推奨）。NGX core：NVIDIA NGX ランタイムに機能の作成を依頼します。", "Signed snippet: nvngx_dlssnr.dll을 직접 호출합니다(권장). NGX core: NVIDIA NGX 런타임에 기능 생성을 요청합니다.") \
    X(TipNvof,            "Hardware optical flow (Turing or newer, nvofapi64.dll from the NVIDIA driver); falls back to block matching when unavailable. Grid = spacing of the hardware vectors in source pixels: 4 px is the fastest and is upsampled to per-pixel vectors, 2 px and 1 px are finer but much slower at 4K.", "硬件光流（Turing 及更新架构，使用 NVIDIA 驱动中的 nvofapi64.dll），不可用时回退到块匹配。网格 = 硬件矢量的间距（源像素）：4 px 最快，会上采样为逐像素矢量；2 px、1 px 更精细，但在 4K 下明显更慢。", "ハードウェアオプティカルフロー（Turing 以降、NVIDIA ドライバーの nvofapi64.dll）。利用できない場合はブロックマッチングにフォールバックします。グリッド = ハードウェアベクトルの間隔（ソースピクセル）：4 px が最速でピクセル単位に補間されます。2 px / 1 px はより精細ですが 4K では大幅に遅くなります。", "하드웨어 옵티컬 플로우(Turing 이상, NVIDIA 드라이버의 nvofapi64.dll). 사용할 수 없으면 블록 매칭으로 대체합니다. 그리드 = 하드웨어 벡터 간격(소스 픽셀): 4 px가 가장 빠르고 픽셀 단위로 보간됩니다. 2 px / 1 px는 더 세밀하지만 4K에서는 훨씬 느립니다.") \
    X(TipAutoReset,       "Clear the temporal history when the matching cost jumps sharply (scene cut). Off by default: DLSS 5 recovers on its own, and every reset causes a visible pop.", "匹配代价突然跳变（镜头切换）时清空时间历史。默认关闭：DLSS 5 会自行恢复，而每次重置都会造成明显的画面跳动。", "マッチングコストが急激に跳ね上がったとき（シーンカット）に時間履歴をクリアします。既定ではオフ：DLSS 5 は自力で回復し、リセットのたびに目に見えるポップが生じます。", "매칭 비용이 급격히 뛸 때(장면 전환) 시간 히스토리를 지웁니다. 기본값은 꺼짐: DLSS 5는 스스로 복구되며, 초기화할 때마다 눈에 띄는 튐이 생깁니다.") \
    X(TipKeepAlpha,       "Store the camera's alpha channel (transparent background when VRChat streams with transparency).", "保存相机的 Alpha 通道（VRChat 以透明背景串流时保留透明）。", "カメラのアルファチャンネルを保存します（VRChat が透過付きで配信している場合は背景が透明になります）。", "카메라의 알파 채널을 저장합니다(VRChat이 투명 배경으로 스트리밍할 때 투명 유지).") \
    X(TipTimelapse,       "Automatically save a photo at this interval while a source is connected.", "连接到源时按此间隔自动拍照。", "ソース接続中、この間隔で自動的に写真を保存します。", "소스가 연결된 동안 이 간격으로 자동 촬영합니다.") \
    X(TipHotkey,          "Works while VRChat is in the foreground.", "VRChat 在前台时也可使用。", "VRChat が前面にあるときでも動作します。", "VRChat이 앞에 있을 때도 동작합니다.") \
    X(TipSearchRadius,    "Full-search radius at quarter resolution (larger = faster motion tracked, slower).", "四分之一分辨率下的全搜索半径（越大可追踪越快的运动，但更慢）。", "1/4 解像度での全探索半径（大きいほど速い動きを追跡できますが遅くなります）。", "1/4 해상도에서의 전체 탐색 반경(클수록 빠른 움직임을 추적하지만 느려집니다).") \
    X(TipConfidence,      "Vectors with lower matching confidence are damped towards zero.", "匹配置信度低于此值的矢量会被衰减为零。", "マッチング信頼度がこれより低いベクトルはゼロに向けて減衰されます。", "매칭 신뢰도가 이보다 낮은 벡터는 0으로 감쇠됩니다.") \
    X(TipCutThreshold,    "Average matching cost (0–1 luma) above which a scene cut is assumed.", "平均匹配代价（0–1 亮度）超过此值时视为场景切换。", "平均マッチングコスト（0–1 輝度）がこの値を超えるとシーンカットとみなします。", "평균 매칭 비용(0–1 휘도)이 이 값을 넘으면 장면 전환으로 간주합니다.") \
    X(TipDlaaPreset,      "DLSS render preset (K = transformer model, default).", "DLSS 渲染预设（K = Transformer 模型，默认）。", "DLSS レンダープリセット（K = Transformer モデル、既定）。", "DLSS 렌더 프리셋(K = 트랜스포머 모델, 기본값).") \
    X(Frames,             "Processed frames", "已处理帧", "処理済みフレーム", "처리된 프레임") \
    X(Resets,             "History resets", "历史重置次数", "履歴リセット回数", "히스토리 초기화 횟수") \
    X(Licenses,           "Third-party notices", "第三方声明", "サードパーティ表記", "서드파티 고지") \
    X(NgxStatus,          "NGX", "NGX", "NGX", "NGX") \
    X(Nvof,               "Optical flow", "光流", "オプティカルフロー", "옵티컬 플로우") \
    X(SceneCut,           "Scene cut", "场景切换", "シーンカット", "장면 전환") \
    X(Capturing,          "Capturing…", "正在拍照…", "撮影中…", "촬영 중…") \
    X(Pending,            "pending", "处理中", "処理中", "대기 중") \
    X(Interval,           "Interval", "间隔", "間隔", "간격") \
    X(Seconds,            "s", "秒", "秒", "초") \
    X(CaptureFailed,      "Capture failed", "拍照失败", "撮影に失敗しました", "촬영 실패") \
    X(RuntimeLoadFailed,  "Failed to load the DLSS 5 runtime", "DLSS 5 运行库加载失败", "DLSS 5 ランタイムの読み込みに失敗しました", "DLSS 5 런타임을 로드하지 못했습니다") \
    X(RuntimeLoadedToast, "DLSS 5 runtime loaded", "DLSS 5 运行库已加载", "DLSS 5 ランタイムを読み込みました", "DLSS 5 런타임을 로드했습니다") \
    X(HistoryReset,       "Temporal history reset", "已重置时间历史", "時間履歴をリセットしました", "시간 이력을 재설정했습니다") \
    X(HotkeyFailed,       "Could not register the capture hotkey (already in use?)", "无法注册拍照快捷键（可能已被占用）", "撮影ホットキーを登録できません（使用中の可能性）", "촬영 단축키를 등록할 수 없습니다(이미 사용 중?)") \
    X(InitFailed,         "Initialization failed", "初始化失败", "初期化に失敗しました", "초기화 실패") \
    X(ResetAllSettings,   "Reset all settings", "重置所有设置", "すべての設定をリセット", "모든 설정 초기화") \
    X(SettingsReset,      "Settings restored to defaults", "设置已恢复默认", "設定を既定値に戻しました", "설정이 기본값으로 복원되었습니다") \
    X(Ok,                 "OK", "确定", "OK", "확인") \
    X(Cancel,             "Cancel", "取消", "キャンセル", "취소") \
    X(HdrSource,          "HDR source (floating-point input)", "HDR 源（浮点输入）", "HDR ソース（浮動小数点入力）", "HDR 소스(부동소수점 입력)") \
    X(PaperWhite,         "Paper white", "纸白（曝光基准）", "ペーパーホワイト", "페이퍼 화이트") \
    X(TipPaperWhite,      "Scene value that maps to display white before the neural pass. Raise it to darken an over-bright HDR feed, lower it to brighten a dark one.", "神经渲染前映射到显示白的场景亮度值。调高可压暗过亮的 HDR 画面，调低可提亮偏暗的画面。", "ニューラルパスの前に表示白へ対応付けるシーン輝度値。明るすぎる HDR 映像は上げて暗く、暗い映像は下げて明るくします。", "뉴럴 패스 전에 디스플레이 화이트로 매핑되는 장면 밝기 값입니다. 너무 밝은 HDR 영상은 올려서 어둡게, 어두운 영상은 내려서 밝게 합니다.") \
    X(HighlightCompression, "Highlight compression", "高光压缩", "ハイライト圧縮", "하이라이트 압축") \
    X(TipHighlightCompression, "Soft roll-off for values above white instead of clipping them (0 = hard clip, 1 = full roll-off).", "对超过白点的亮度做柔和过渡而不是直接裁剪（0 = 硬裁剪，1 = 完全柔化）。", "白を超える値をクリップせず滑らかに丸めます（0 = ハードクリップ、1 = 完全なロールオフ）。", "화이트를 넘는 값을 잘라내지 않고 부드럽게 눌러 줍니다(0 = 하드 클립, 1 = 완전한 롤오프).") \
    X(HdrSourceHint,      "The neural network works on SDR sRGB frames, so this floating-point source is tone-compressed to SDR here. 8-bit sources are used as-is.", "神经网络只处理 SDR sRGB 画面，因此当前浮点源会在此压缩为 SDR。8 位源按原样使用。", "ニューラルネットワークは SDR sRGB フレームを処理するため、この浮動小数点ソースはここで SDR にトーン圧縮されます。8 ビットソースはそのまま使われます。", "뉴럴 네트워크는 SDR sRGB 프레임을 처리하므로 이 부동소수점 소스는 여기서 SDR로 톤 압축됩니다. 8비트 소스는 그대로 사용됩니다.") \
    X(NoDisplay,          "No frame yet", "尚无画面", "まだフレームがありません", "아직 프레임이 없습니다") \
    X(Language,           "Language", "语言", "言語", "언어") \
    X(LangAuto,           "Auto (system)", "自动（跟随系统）", "自動（システム）", "자동(시스템)") \
    X(Capture,            "Capture photo", "拍照", "撮影", "촬영") \
    X(CaptureHint,        "Save a lossless PNG of the current DLSS 5 frame", "将当前 DLSS 5 画面保存为无损 PNG", "現在の DLSS 5 フレームを無損失 PNG で保存", "현재 DLSS 5 프레임을 무손실 PNG로 저장") \
    X(StatusConnected,    "Connected", "已连接", "接続済み", "연결됨") \
    X(StatusWaiting,      "Waiting for VRChat Spout stream…", "等待 VRChat Spout 串流…", "VRChat の Spout ストリームを待機中…", "VRChat Spout 스트림 대기 중…") \
    X(StatusNoSpout,      "No Spout sender found", "未找到 Spout 发送端", "Spout 送信元が見つかりません", "Spout 송신자를 찾을 수 없음") \
    X(SecSource,          "Source", "视频源", "ソース", "소스") \
    X(SecNeural,          "DLSS 5 Neural Rendering", "DLSS 5 神经渲染", "DLSS 5 ニューラルレンダリング", "DLSS 5 뉴럴 렌더링") \
    X(SecGuidance,        "Frame guidance (motion & depth)", "帧引导（运动与深度）", "フレームガイダンス（モーション・深度）", "프레임 가이던스(모션·깊이)") \
    X(SecDlaa,            "DLAA pre-pass", "DLAA 预处理", "DLAA プリパス", "DLAA 프리패스") \
    X(SecCapture,         "Capture", "拍照", "撮影", "촬영") \
    X(SecDisplay,         "Display", "显示", "表示", "표시") \
    X(SecAbout,           "About", "关于", "情報", "정보") \
    X(Sender,             "Spout sender", "Spout 发送端", "Spout 送信元", "Spout 송신자") \
    X(SenderAuto,         "Auto (VRChat)", "自动（VRChat）", "自動（VRChat）", "자동(VRChat)") \
    X(Refresh,            "Refresh", "刷新", "更新", "새로 고침") \
    X(Detected,           "Detected", "检测到", "検出", "감지됨") \
    X(SenderFps,          "Sender FPS", "发送端帧率", "送信元 FPS", "송신자 FPS") \
    X(Format,             "Format", "格式", "フォーマット", "형식") \
    X(CustomResolution,   "Custom processing resolution", "自定义处理分辨率", "処理解像度をカスタム指定", "사용자 지정 처리 해상도") \
    X(CustomResolutionHint, "Off = adaptive: follows the VRChat stream resolution automatically", "关闭 = 自适应：自动跟随 VRChat 串流分辨率", "オフ = 自動：VRChat のストリーム解像度に追従します", "끄기 = 자동: VRChat 스트림 해상도를 따릅니다") \
    X(Width,              "Width", "宽度", "幅", "너비") \
    X(Height,             "Height", "高度", "高さ", "높이") \
    X(KeepAspect,         "Keep aspect ratio", "保持宽高比", "アスペクト比を維持", "가로세로 비율 유지") \
    X(Presets,            "Presets", "预设", "プリセット", "프리셋") \
    X(HowToEnable,        "Enable in VRChat: Camera → Stream mode → turn on \"Spout Stream\"", "在 VRChat 中启用：相机 → Stream（串流）模式 → 打开“Spout Stream”", "VRChat での有効化：カメラ → Stream モード → 「Spout Stream」をオン", "VRChat에서 켜기: 카메라 → Stream 모드 → \"Spout Stream\" 켜기") \
    X(VrchatResHint,      "Stream resolution is set in VRChat's camera settings (720p–2160p) or with camera_spout_res_width/height in config.json", "串流分辨率在 VRChat 相机设置中选择（720p–2160p），或通过 config.json 的 camera_spout_res_width/height 自定义", "ストリーム解像度は VRChat のカメラ設定（720p～2160p）または config.json の camera_spout_res_width/height で指定します", "스트림 해상도는 VRChat 카메라 설정(720p~2160p) 또는 config.json의 camera_spout_res_width/height로 지정합니다") \
    X(NrEnable,           "Enable DLSS 5 (DLSSNR)", "启用 DLSS 5（DLSSNR）", "DLSS 5（DLSSNR）を有効化", "DLSS 5(DLSSNR) 사용") \
    X(NrHint,             "Adjustable DLSS 5 parameters (the same set exposed by the RenoDX add-on)", "可调节的 DLSS 5 参数（与 RenoDX 插件提供的相同）", "調整可能な DLSS 5 パラメーター（RenoDX アドオンと同じ項目）", "조정 가능한 DLSS 5 매개변수(RenoDX 애드온과 동일한 항목)") \
    X(Runtime,            "Runtime", "运行库", "ランタイム", "런타임") \
    X(RuntimeMissing,     "nvngx_dlssnr.dll not found. Place it next to the executable or choose its path below.", "未找到 nvngx_dlssnr.dll。请将其放到程序目录，或在下方选择路径。", "nvngx_dlssnr.dll が見つかりません。実行ファイルと同じフォルダに置くか、下でパスを指定してください。", "nvngx_dlssnr.dll을 찾을 수 없습니다. 실행 파일 옆에 두거나 아래에서 경로를 선택하세요.") \
    X(RuntimePath,        "Runtime path", "运行库路径", "ランタイムのパス", "런타임 경로") \
    X(Browse,             "Browse…", "浏览…", "参照…", "찾아보기…") \
    X(Route,              "Host route", "加载方式", "ホスト方式", "호스트 방식") \
    X(RouteSnippet,       "Direct (signed snippet)", "直接加载（signed snippet）", "直接（signed snippet）", "직접(signed snippet)") \
    X(RouteCore,          "NGX core (experimental)", "NGX 核心（实验性）", "NGX コア（実験的）", "NGX 코어(실험적)") \
    X(Preset,             "Preset", "预设", "プリセット", "프리셋") \
    X(Style,              "Style", "风格", "スタイル", "스타일") \
    X(StyleDefault,       "Default", "默认", "既定", "기본") \
    X(StyleNatural,       "Natural", "自然", "ナチュラル", "내추럴") \
    X(StyleCinematic,     "Cinematic", "电影感", "シネマティック", "시네마틱") \
    X(Intensity,          "Intensity", "强度", "強度", "강도") \
    X(GlobalTone,         "Global tone strength", "全局色调强度", "グローバルトーン強度", "글로벌 톤 강도") \
    X(LocalTone,          "Local tone strength", "局部色调强度", "ローカルトーン強度", "로컬 톤 강도") \
    X(LocalStructure,     "Local structure strength", "局部结构强度", "ローカル構造強度", "로컬 구조 강도") \
    X(SkinStructure,      "Skin structure strength", "皮肤结构强度", "スキン構造強度", "피부 구조 강도") \
    X(UseDefault,         "Runtime default", "使用默认值", "既定値を使用", "기본값 사용") \
    X(AutoMask,           "Auto mask", "自动遮罩", "自動マスク", "자동 마스크") \
    X(UiCorrection,       "UI correction", "UI 校正", "UI 補正", "UI 보정") \
    X(NrUpscale,          "Neural upscaling to the custom resolution (experimental)", "神经上采样到自定义分辨率（实验性）", "カスタム解像度へニューラルアップスケール（実験的）", "사용자 지정 해상도로 뉴럴 업스케일(실험적)") \
    X(ResetHistory,       "Reset temporal history", "重置时序历史", "時間履歴をリセット", "시간 이력 초기화") \
    X(ResetDefaults,      "Reset to defaults", "恢复默认", "既定値に戻す", "기본값으로 재설정") \
    X(Status,             "Status", "状态", "状態", "상태") \
    X(Active,             "Active", "运行中", "動作中", "동작 중") \
    X(Inactive,           "Inactive", "未运行", "停止中", "비활성") \
    X(Failed,             "Failed", "失败", "失敗", "실패") \
    X(GpuTime,            "GPU time", "GPU 耗时", "GPU 時間", "GPU 시간") \
    X(MotionSource,       "Motion vectors", "运动矢量", "モーションベクトル", "모션 벡터") \
    X(MotionZero,         "None (zero)", "无（零）", "なし（ゼロ）", "없음(0)") \
    X(MotionCompute,      "GPU block matching (built-in)", "GPU 块匹配（内置）", "GPU ブロックマッチング（内蔵）", "GPU 블록 매칭(내장)") \
    X(MotionNvof,         "NVIDIA Optical Flow (hardware, recommended)", "NVIDIA 光流（硬件，推荐）", "NVIDIA Optical Flow（ハードウェア・推奨）", "NVIDIA Optical Flow(하드웨어, 권장)") \
    X(DepthSource,        "Depth", "深度", "深度", "깊이") \
    X(DepthFlat,          "Flat", "平面", "フラット", "평면") \
    X(DepthGradient,      "Gradient (placeholder)", "渐变（占位）", "グラデーション（代替）", "그라데이션(대체)") \
    X(DepthZero,          "Zero", "零", "ゼロ", "0") \
    X(DepthEstimated,     "AI estimated (Depth Anything V2)", "AI 估计（Depth Anything V2）", "AI 推定（Depth Anything V2）", "AI 추정(Depth Anything V2)") \
    X(DepthInterval,      "Depth update interval", "深度更新间隔", "深度更新間隔", "깊이 갱신 간격") \
    X(DepthResolution,    "Depth network resolution", "深度网络分辨率", "深度ネットワーク解像度", "깊이 네트워크 해상도") \
    X(DepthModel,         "Depth model", "深度模型", "深度モデル", "깊이 모델") \
    X(DepthStatus,        "Depth estimator", "深度估计器", "深度推定", "깊이 추정기") \
    X(DepthInitializing,  "initializing…", "正在初始化…", "初期化中…", "초기화 중…") \
    X(DepthReady,         "ready", "就绪", "準備完了", "준비됨") \
    X(DepthUnavailable,   "unavailable, zero depth is used", "不可用，改用零深度", "利用不可、ゼロ深度を使用", "사용 불가, 0 깊이 사용") \
    X(DepthModelMissing,  "Depth model not found. Re-extract the release package (models\\depth_anything_v2_small_fp16.onnx) or select a model file.", "未找到深度模型。请重新解压发布包（models\\depth_anything_v2_small_fp16.onnx）或选择模型文件。", "深度モデルが見つかりません。リリースパッケージを再展開する（models\\depth_anything_v2_small_fp16.onnx）か、モデルファイルを選択してください。", "깊이 모델을 찾을 수 없습니다. 릴리스 패키지를 다시 압축 해제하거나(models\\depth_anything_v2_small_fp16.onnx) 모델 파일을 선택하세요.") \
    X(NvofBidirectional,  "Bidirectional consistency check", "双向一致性检查", "双方向一貫性チェック", "양방향 일관성 검사") \
    X(TipNvofBidirectional, "Also computes the backward flow and lowers the confidence where forward and backward vectors disagree (occlusions, noise). Recommended. Driver API 5.0 computes both directions in one pass; older drivers run a second pass (about twice the flow time).", "同时计算反向光流，在正向与反向矢量不一致处（遮挡、噪声）降低置信度。推荐开启。驱动 API 5.0 一次即可算出双向光流；旧驱动需要第二遍（光流耗时约翻倍）。", "逆方向のフローも計算し、順方向と逆方向のベクトルが一致しない箇所（オクルージョン、ノイズ）の信頼度を下げます。推奨。ドライバー API 5.0 では一度に双方向を計算します。古いドライバーでは 2 回目のパスが必要です（フロー時間は約 2 倍）。", "역방향 플로우도 계산하여 정방향과 역방향 벡터가 일치하지 않는 곳(가림, 노이즈)의 신뢰도를 낮춥니다. 권장. 드라이버 API 5.0은 한 번에 양방향을 계산하고, 구형 드라이버는 두 번째 패스가 필요합니다(플로우 시간 약 2배).") \
    X(CompareDepth,       "Depth", "深度", "深度", "깊이") \
    X(TipDepthInterval,   "Run the depth network every N processed frames; in between, the previous depth is carried along with the motion vectors.", "每 N 个处理帧运行一次深度网络；其间用运动矢量搬运上一次的深度。", "N 処理フレームごとに深度ネットワークを実行し、その間は前回の深度をモーションベクトルで移動させます。", "처리된 N 프레임마다 깊이 네트워크를 실행하고, 그 사이에는 이전 깊이를 모션 벡터로 이동시킵니다.") \
    X(TipDepthResolution, "Long side of the picture given to the depth network. Larger = finer depth edges, slower.", "送入深度网络的画面长边。越大深度边缘越精细，但更慢。", "深度ネットワークに渡す画像の長辺。大きいほど深度の輪郭が細かくなりますが遅くなります。", "깊이 네트워크에 전달되는 화면의 긴 변. 클수록 깊이 경계가 세밀해지지만 느려집니다.") \
    X(Inference,          "Inference", "推理", "推論", "추론") \
    X(SearchRadius,       "Search radius", "搜索半径", "探索半径", "탐색 반경") \
    X(MotionConfidence,   "Confidence threshold", "置信度阈值", "信頼度しきい値", "신뢰도 임계값") \
    X(NvofGrid,           "Flow grid", "光流网格", "フローグリッド", "플로우 그리드") \
    X(NvofPerf,           "Flow quality", "光流质量", "フロー品質", "플로우 품질") \
    X(PerfSlow,           "Slow (best)", "慢（最佳）", "低速（最高品質）", "느림(최고)") \
    X(PerfMedium,         "Medium", "中", "中", "중간") \
    X(PerfFast,           "Fast", "快", "高速", "빠름") \
    X(AutoReset,          "Auto reset on scene cut", "场景切换时自动重置", "シーン切替時に自動リセット", "장면 전환 시 자동 초기화") \
    X(CutThreshold,       "Cut threshold", "切换阈值", "切替しきい値", "전환 임계값") \
    X(FrameCost,          "Frame mismatch", "帧差异", "フレーム不一致", "프레임 불일치") \
    X(DlaaEnable,         "Run DLSS DLAA before neural rendering", "在神经渲染前运行 DLSS DLAA", "ニューラルレンダリングの前に DLSS DLAA を実行", "뉴럴 렌더링 전에 DLSS DLAA 실행") \
    X(DlaaHint,           "Uses the official nvngx_dlss.dll (temporal anti-aliasing). Adds GPU cost.", "使用官方 nvngx_dlss.dll（时序抗锯齿），会增加 GPU 开销。", "公式の nvngx_dlss.dll（時間的アンチエイリアス）を使用します。GPU 負荷が増えます。", "공식 nvngx_dlss.dll(시간적 안티앨리어싱)을 사용합니다. GPU 부하가 증가합니다.") \
    X(DlaaPreset,         "DLSS preset", "DLSS 预设", "DLSS プリセット", "DLSS 프리셋") \
    X(CaptureFolder,      "Folder", "保存文件夹", "保存先", "저장 폴더") \
    X(OpenFolder,         "Open", "打开", "開く", "열기") \
    X(KeepAlpha,          "Keep transparency (alpha)", "保留透明度（Alpha）", "透明度（アルファ）を保持", "투명도(알파) 유지") \
    X(SaveOriginal,       "Also save the original frame", "同时保存原始画面", "元のフレームも保存", "원본 프레임도 저장") \
    X(Hotkey,             "Global hotkey", "全局热键", "グローバルホットキー", "전역 단축키") \
    X(Timelapse,          "Auto capture every (seconds)", "自动拍照间隔（秒）", "自動撮影の間隔（秒）", "자동 촬영 간격(초)") \
    X(TimelapseOff,       "0 = off", "0 = 关闭", "0 = オフ", "0 = 끔") \
    X(LastCapture,        "Last capture", "上次拍照", "前回の撮影", "마지막 촬영") \
    X(Saved,              "Saved", "已保存", "保存しました", "저장됨") \
    X(SaveFailed,         "Save failed", "保存失败", "保存に失敗しました", "저장 실패") \
    X(CaptureNoFrame,     "No frame to capture yet", "尚无可拍摄的画面", "撮影できるフレームがありません", "촬영할 프레임이 없습니다") \
    X(Compare,            "Compare", "对比", "比較", "비교") \
    X(CompareOutput,      "DLSS 5 output", "DLSS 5 输出", "DLSS 5 出力", "DLSS 5 출력") \
    X(CompareOriginal,    "Original", "原始画面", "オリジナル", "원본") \
    X(CompareWipe,        "Wipe (drag the divider)", "分割对比（拖动分隔线）", "ワイプ（境界をドラッグ）", "와이프(구분선 드래그)") \
    X(Checkerboard,       "Checkerboard behind transparency", "透明区域显示棋盘格", "透明部分にチェッカーボード", "투명 영역에 체커보드") \
    X(FitWindowLabel,     "Fit to window", "适应窗口", "ウィンドウに合わせる", "창에 맞춤") \
    X(OneToOne,           "1:1 pixels", "1:1 像素", "1:1 ピクセル", "1:1 픽셀") \
    X(Vsync,              "V-Sync", "垂直同步", "垂直同期", "수직 동기화") \
    X(Overlay,            "Show overlay info", "显示叠加信息", "オーバーレイ情報を表示", "오버레이 정보 표시") \
    X(ShowLog,            "Show log", "显示日志", "ログを表示", "로그 표시") \
    X(Sidebar,            "Sidebar", "侧边栏", "サイドバー", "사이드바") \
    X(AboutText,          "Real-time DLSS 5 neural rendering for VRChat's Spout camera stream, with lossless photo capture.", "为 VRChat Spout 相机串流提供实时 DLSS 5 神经渲染，并支持无损拍照。", "VRChat の Spout カメラストリームにリアルタイムで DLSS 5 ニューラルレンダリングを適用し、無損失で撮影できます。", "VRChat Spout 카메라 스트림에 실시간 DLSS 5 뉴럴 렌더링을 적용하고 무손실로 촬영합니다.") \
    X(Version,            "Version", "版本", "バージョン", "버전") \
    X(Gpu,                "GPU", "显卡", "GPU", "GPU") \
    X(Driver,             "Driver", "驱动", "ドライバー", "드라이버") \
    X(OpenLogFile,        "Open log file", "打开日志文件", "ログファイルを開く", "로그 파일 열기") \
    X(OpenSettingsFolder, "Open settings folder", "打开设置文件夹", "設定フォルダを開く", "설정 폴더 열기") \
    X(ProjectPage,        "Project page", "项目主页", "プロジェクトページ", "프로젝트 페이지") \
    X(Fps,                "FPS", "帧率", "FPS", "FPS") \
    X(Source,             "Source", "源", "ソース", "소스") \
    X(Output,             "Output", "输出", "出力", "출력") \
    X(Processing,         "Processing", "处理中", "処理中", "처리 중") \
    X(Converged,          "Processed", "处理完成", "処理済み", "처리 완료") \
    X(Bypass,             "Bypass (original)", "直通（原始画面）", "バイパス（オリジナル）", "우회(원본)") \
    X(LogTitle,           "Log", "日志", "ログ", "로그") \
    X(Clear,              "Clear", "清除", "クリア", "지우기") \
    X(Close,              "Close", "关闭", "閉じる", "닫기") \
    X(DeviceRemoved,      "The graphics device was lost. Please restart the application.", "图形设备已丢失，请重新启动应用。", "グラフィックデバイスが失われました。アプリを再起動してください。", "그래픽 장치가 손실되었습니다. 앱을 다시 시작하세요.") \
    X(ErrorTitle,         "Error", "错误", "エラー", "오류") \
    X(NrNotNvidia,        "An NVIDIA RTX GPU is required for DLSS 5.", "DLSS 5 需要 NVIDIA RTX 显卡。", "DLSS 5 には NVIDIA RTX GPU が必要です。", "DLSS 5에는 NVIDIA RTX GPU가 필요합니다.") \
    X(Experimental,       "Experimental", "实验性", "実験的", "실험적") \
    X(Apply,              "Apply", "应用", "適用", "적용") \
    X(Key,                "Key", "按键", "キー", "키") \
    X(PreviewHint,        "Start VRChat, open the camera, switch to Stream mode and enable Spout Stream.", "启动 VRChat，打开相机，切换到 Stream 模式并开启 Spout Stream。", "VRChat を起動し、カメラを開いて Stream モードに切り替え、Spout Stream を有効にしてください。", "VRChat를 실행하고 카메라를 열어 Stream 모드로 전환한 뒤 Spout Stream을 켜세요.") \
    X(TipIntensity,       "Overall strength of the neural rendering (1 = default, 0 = off). The runtime itself stops at 1; above 1 the app amplifies the difference between the neural result and the original picture (the highest of the five strengths above 1 sets the gain).", "神经渲染的整体强度（1 = 默认，0 = 关闭）。运行库本身最高只到 1；大于 1 时程序放大神经结果与原图之间的差异（五个强度中最高的那个决定放大倍数）。", "ニューラルレンダリングの全体強度（1 = 既定、0 = オフ）。ランタイム自体は 1 が上限で、1 を超えるとアプリがニューラル結果と元画像との差を増幅します（5 つの強度のうち最も高い値が倍率になります）。", "뉴럴 렌더링의 전체 강도(1 = 기본, 0 = 끔). 런타임 자체는 1까지이며, 1을 넘으면 앱이 뉴럴 결과와 원본의 차이를 증폭합니다(다섯 강도 중 가장 높은 값이 배율이 됩니다).") \
    X(TipGlobalTone,      "How much the model may change global exposure and colour (up to 1 in the runtime). Above 1 the app amplifies the difference between the neural result and the original, like the intensity.", "允许模型改变全局曝光与色彩的程度（运行库内最高 1）。大于 1 时与强度一样，程序放大神经结果与原图之间的差异。", "モデルが全体の露出と色をどれだけ変えられるか（ランタイム内では 1 まで）。1 を超えると強度と同様に、アプリがニューラル結果と元画像との差を増幅します。", "모델이 전체 노출과 색을 얼마나 바꿀 수 있는지(런타임에서는 1까지). 1을 넘으면 강도와 마찬가지로 앱이 뉴럴 결과와 원본의 차이를 증폭합니다.") \
    X(TipLocalTone,       "Local contrast and lighting adjustments (up to 1 in the runtime). Above 1 the app amplifies the difference between the neural result and the original, like the intensity.", "局部对比度与光照调整（运行库内最高 1）。大于 1 时与强度一样，程序放大神经结果与原图之间的差异。", "ローカルコントラストとライティングの調整（ランタイム内では 1 まで）。1 を超えると強度と同様に、アプリがニューラル結果と元画像との差を増幅します。", "로컬 대비와 조명 조정(런타임에서는 1까지). 1을 넘으면 강도와 마찬가지로 앱이 뉴럴 결과와 원본의 차이를 증폭합니다.") \
    X(TipLocalStructure,  "Fine detail and texture enhancement (up to 1 in the runtime). Above 1 the app amplifies the difference between the neural result and the original, like the intensity.", "细节与纹理增强（运行库内最高 1）。大于 1 时与强度一样，程序放大神经结果与原图之间的差异。", "ディテールとテクスチャの強調（ランタイム内では 1 まで）。1 を超えると強度と同様に、アプリがニューラル結果と元画像との差を増幅します。", "세부 묘사와 질감 강조(런타임에서는 1까지). 1을 넘으면 강도와 마찬가지로 앱이 뉴럴 결과와 원본의 차이를 증폭합니다.") \
    X(TipSkinStructure,   "Detail on skin-like surfaces (up to 1 in the runtime). \"Runtime default\" leaves it to the model. Above 1 the app amplifies the difference between the neural result and the original, like the intensity.", "皮肤类表面的细节（运行库内最高 1）。“使用默认值”由模型自行决定。大于 1 时与强度一样，程序放大神经结果与原图之间的差异。", "肌のような表面のディテール（ランタイム内では 1 まで）。「既定値を使用」でモデルに任せます。1 を超えると強度と同様に、アプリがニューラル結果と元画像との差を増幅します。", "피부 같은 표면의 디테일(런타임에서는 1까지). \"기본값 사용\"이면 모델에 맡깁니다. 1을 넘으면 강도와 마찬가지로 앱이 뉴럴 결과와 원본의 차이를 증폭합니다.") \
    X(TipPreset,          "Model preset 0-3 (0 = default).", "模型预设 0–3（0 = 默认）。", "モデルプリセット 0～3（0 = 既定）。", "모델 프리셋 0~3(0 = 기본).") \
    X(TipStyle,           "Rendering look: Default, Natural or Cinematic.", "渲染风格：默认、自然或电影感。", "レンダリングの雰囲気：既定、ナチュラル、シネマティック。", "렌더링 스타일: 기본, 내추럴, 시네마틱.") \
    X(TipAutoMask,        "Let the runtime mask regions it should not change.", "让运行库自动遮罩不应改变的区域。", "変更すべきでない領域をランタイムに自動でマスクさせます。", "런타임이 변경하지 말아야 할 영역을 자동으로 마스크합니다.") \
    X(TipUiCorrection,    "Protect flat UI-like regions from being altered.", "保护平面 UI 类区域不被改动。", "UI のような平坦な領域を保護します。", "UI 같은 평면 영역을 보호합니다.") \
    X(TipMotion,          "Motion vectors tell DLSS 5 how the image moved since the previous frame (better temporal stability).", "运动矢量告诉 DLSS 5 画面相对上一帧的移动（提升时序稳定性）。", "モーションベクトルは前フレームからの動きを DLSS 5 に伝えます（時間的安定性が向上）。", "모션 벡터는 이전 프레임 대비 움직임을 DLSS 5에 전달합니다(시간적 안정성 향상).") \
    X(TipDepth,           "DLSS 5 uses depth to separate subject and background. VRChat's stream has none, so Depth Anything V2 estimates it from the picture on the GPU (DirectML). Flat / gradient / zero are simple placeholders.", "DLSS 5 用深度区分主体与背景。VRChat 串流没有深度，因此用 Depth Anything V2 在 GPU（DirectML）上从画面估计深度。平面 / 渐变 / 零只是简单的占位值。", "DLSS 5 は深度で被写体と背景を区別します。VRChat のストリームには深度がないため、Depth Anything V2 が GPU（DirectML）上で画像から深度を推定します。フラット / グラデーション / ゼロは単純な代替値です。", "DLSS 5는 깊이로 피사체와 배경을 구분합니다. VRChat 스트림에는 깊이가 없으므로 Depth Anything V2가 GPU(DirectML)에서 화면으로부터 깊이를 추정합니다. 평면 / 그라데이션 / 0은 단순한 대체값입니다.") \
    X(ScaleAuto,          "Adaptive", "自适应", "自動", "자동") \
    X(NoSenders,          "(none)", "（无）", "（なし）", "(없음)") \
    X(NotLoaded,          "Not loaded", "未加载", "未読み込み", "로드되지 않음") \
    X(Loaded,             "Loaded", "已加载", "読み込み済み", "로드됨") \
    X(NotAvailable,       "Not available", "不可用", "利用不可", "사용 불가") \
    X(Available,          "Available", "可用", "利用可能", "사용 가능") \
    X(Unsupported,        "Unsupported", "不支持", "非対応", "지원되지 않음") \
    X(Pixels,             "px", "像素", "px", "px") \
    X(SettingsSaved,      "Settings saved", "设置已保存", "設定を保存しました", "설정이 저장되었습니다") \
    X(CaptureOriginalSuffix, "Also saved the original frame", "已同时保存原始画面", "元のフレームも保存しました", "원본 프레임도 저장했습니다") \
    X(OutputBlend,        "Output blend", "输出混合", "出力ブレンド", "출력 블렌드") \
    X(InputExposure,      "Input exposure (paper-white scale)", "输入曝光（纸白缩放）", "入力露出（ペーパーホワイト倍率）", "입력 노출(페이퍼 화이트 배율)") \
    X(TipInputExposure,   "Gain on the picture the neural pass sees, like scaling the scene's paper white. A brighter input makes the network treat the scene as well lit and relight it more gently; a darker input does the opposite. The output keeps the original brightness, only the character of the result changes.", "神经渲染看到的画面增益，相当于缩放场景纸白。调亮后网络会把场景视为光照充足、重打光更克制；调暗则相反。输出亮度保持原样，只改变结果的风格。", "ニューラルパスが見る画像へのゲインで、シーンのペーパーホワイトを拡大縮小するのと同じです。明るくするとネットワークは十分に照らされたシーンとみなしてリライトを控えめにし、暗くすると逆になります。出力の明るさは元のままで、結果の性格だけが変わります。", "뉴럴 패스가 보는 화면의 게인으로, 장면의 페이퍼 화이트를 배율 조정하는 것과 같습니다. 밝게 하면 네트워크가 장면을 충분히 밝은 것으로 보고 재조명을 절제하며, 어둡게 하면 반대가 됩니다. 출력 밝기는 원본 그대로이고 결과의 성격만 바뀝니다.") \
    X(ToneTransfer,       "Tone transfer strength", "色调传递强度", "トーン転送強度", "톤 전달 강도") \
    X(TipToneTransfer,    "How much of the neural pass's brightness change reaches the output. 0 keeps the original tones, 1 = as rendered, up to 2 exaggerates the change.", "神经渲染带来的明暗变化有多少进入输出。0 = 保留原图明暗，1 = 按渲染结果，最高 2 为加倍夸张。", "ニューラルパスによる明るさの変化をどれだけ出力に反映するか。0 = 元のトーンを維持、1 = レンダリング結果どおり、最大 2 で変化を強調します。", "뉴럴 패스가 바꾼 밝기가 출력에 얼마나 반영되는지. 0 = 원본 톤 유지, 1 = 렌더링 결과 그대로, 최대 2는 변화를 과장합니다.") \
    X(ColorStrength,      "Colour strength", "色彩强度", "色の強度", "색 강도") \
    X(TipColorStrength,   "How much of the neural pass's colour change (hue and saturation) reaches the output. 0 keeps the original colours, 1 = as rendered, up to 2 exaggerates the change.", "神经渲染带来的色彩变化（色相与饱和度）有多少进入输出。0 = 保留原图色彩，1 = 按渲染结果，最高 2 为加倍夸张。", "ニューラルパスによる色の変化（色相と彩度）をどれだけ出力に反映するか。0 = 元の色を維持、1 = レンダリング結果どおり、最大 2 で変化を強調します。", "뉴럴 패스가 바꾼 색(색상과 채도)이 출력에 얼마나 반영되는지. 0 = 원본 색 유지, 1 = 렌더링 결과 그대로, 최대 2는 변화를 과장합니다.") \
    X(SourceMode,         "Input", "输入来源", "入力ソース", "입력 소스") \
    X(SourceSpout,        "VRChat camera (Spout)", "VRChat 相机（Spout）", "VRChat カメラ（Spout）", "VRChat 카메라(Spout)") \
    X(SourceImage,        "Image file", "图片文件", "画像ファイル", "이미지 파일") \
    X(OpenImage,          "Open image...", "打开图片…", "画像を開く…", "이미지 열기…") \
    X(ProcessAndSave,     "Process & save PNG", "处理并保存 PNG", "処理して PNG を保存", "처리 후 PNG 저장") \
    X(NoImage,            "No image opened", "尚未打开图片", "画像が開かれていません", "열린 이미지가 없습니다") \
    X(ImageHint,          "Open a picture (PNG, JPEG, BMP, TIFF, WebP, HEIC...) or drop it onto this window. DLSS 5 refines a still image over several passes; adjust the sliders, then save the result as a lossless PNG.", "打开一张图片（PNG、JPEG、BMP、TIFF、WebP、HEIC…）或直接拖进窗口。DLSS 5 会对静态图片进行多遍收敛处理；调整滑块后，可将结果保存为无损 PNG。", "画像（PNG、JPEG、BMP、TIFF、WebP、HEIC…）を開くか、このウィンドウにドロップしてください。DLSS 5 は静止画を数パスかけて仕上げます。スライダーを調整し、結果をロスレス PNG として保存できます。", "이미지(PNG, JPEG, BMP, TIFF, WebP, HEIC…)를 열거나 이 창에 끌어다 놓으세요. DLSS 5는 정지 이미지를 여러 패스에 걸쳐 다듬습니다. 슬라이더를 조정한 뒤 결과를 무손실 PNG로 저장할 수 있습니다.") \
    X(DropHint,           "Tip: you can also drag & drop an image onto the window.", "提示：也可以把图片直接拖进窗口。", "ヒント：画像をウィンドウにドラッグ＆ドロップすることもできます。", "팁: 이미지를 창에 끌어다 놓아도 됩니다.") \
    X(ImageLoaded,        "Image opened", "图片已打开", "画像を開きました", "이미지를 열었습니다") \
    X(ImageLoadFailed,    "Could not open the image", "无法打开图片", "画像を開けませんでした", "이미지를 열 수 없습니다") \
    X(ImageLabel,         "Image", "图片", "画像", "이미지") \
    X(ImageDownscaled,    "Large picture: processed at a reduced size", "图片过大：已缩小后处理", "大きな画像：縮小して処理します", "큰 이미지: 축소하여 처리합니다") \
    X(CaptureNoImage,     "Open an image first", "请先打开一张图片", "先に画像を開いてください", "먼저 이미지를 여세요") \
    X(ImageFilter,        "Images", "图片", "画像", "이미지") \
    X(UiFps,              "UI", "界面", "UI", "UI") \
    X(ProcessingFps,      "processing", "处理", "処理", "처리") \
    X(TipUiFps,           "Interface refresh rate. The interface runs on its own queue and thread, so it stays smooth even when processing is slow.", "界面刷新率。界面在独立的队列和线程上运行，即使处理很慢也能保持流畅。", "インターフェースの更新レート。UI は独自のキューとスレッドで動作するため、処理が遅くても滑らかなままです。", "인터페이스 갱신률. UI는 별도의 큐와 스레드에서 실행되므로 처리가 느려도 부드럽게 유지됩니다.") \
    X(SourceModeHint,     "Process the VRChat camera stream live, or open a picture from disk and run it through DLSS 5.", "实时处理 VRChat 相机串流，或打开磁盘上的图片交给 DLSS 5 处理。", "VRChat のカメラストリームをリアルタイムで処理するか、ディスク上の画像を開いて DLSS 5 で処理します。", "VRChat 카메라 스트림을 실시간으로 처리하거나, 디스크의 이미지를 열어 DLSS 5로 처리합니다.")

enum class Str {
#define VDC_STR_ENUM(id, en, zh, ja, ko) id,
    VDC_STRING_LIST(VDC_STR_ENUM)
#undef VDC_STR_ENUM
    Count
};

namespace I18n {
void        SetLanguage(Lang lang);
Lang        Current();
Lang        Detect();                      // from the Windows UI language
Lang        FromSetting(int setting);      // 0 auto, 1 en, 2 zh, 3 ja, 4 ko
const char* T(Str id);
const char* LanguageName(Lang lang);       // native name
} // namespace I18n

#define TR(id) ::vdc::I18n::T(::vdc::Str::id)

} // namespace vdc
