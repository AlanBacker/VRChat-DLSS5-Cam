<p align="center">
  <img src="../resources/app-256.png" width="96" alt="VRChat DLSS5 Cam 图标">
</p>

# VRChat DLSS5 Cam

[English](../README.md) · **简体中文** · [日本語](README.ja.md) · [한국어](README.ko.md)

VRChat DLSS5 Cam 会捕获 VRChat 内置相机（Stream 相机并开启 *Spout Stream*）的画面，
实时用 **DLSS 5 神经渲染（DLSSNR）** 进行处理，即时显示结果，并保存无损 PNG 照片。
它是独立的 Windows 应用：不向 VRChat 注入任何代码，也不需要 Mod。

> **状态：早期预览版。** 二进制文件由 GitHub Actions 构建，处理流水线尚未在大量 GPU 与不同版本的 DLSS 5
> 运行库上充分验证。遇到问题请附上 `log.txt` 提交 Issue。

## 功能

- **实时预览** DLSS 5 处理后的 VRChat 相机画面，支持擦除对比、原图、运动矢量和深度视图。
- **DLSS 5 神经渲染** 直接从 `nvngx_dlssnr.dll` 加载，提供与 RenoDX 的 DLSS 5 ReShade 插件相同的参数：
  预设、风格、强度、全局色调、局部色调、局部结构、皮肤结构、自动遮罩和 UI 修正。参数即时生效，也可以在定格画面上调节。
- **帧引导。** DLSSNR 是时域模型，需要运动矢量和深度。本程序提供带正反向一致性检查的
  **NVIDIA Optical Flow** 运动矢量，以及由 **Depth Anything V2** 在 GPU 上估计的深度图（ONNX Runtime + DirectML）。
  GPU 块匹配和占位深度仍可作为回退方案，另外还可以在神经渲染之前增加一次 **DLAA**（原生分辨率的 DLSS 抗锯齿）。
- **自适应分辨率。** 自动检测发送端分辨率；可设置自定义输出分辨率，并可让 DLSS 5 直接放大到该分辨率。可选的镜头切换检测会重置时域历史。
- **无损拍照。** 将处理后的画面（可选同时保存原图）保存为 PNG，全局快捷键（默认 `Ctrl+Alt+P`）在 VRChat 处于前台时同样有效，并支持定时连拍。
- **四种语言**（English、简体中文、日本語、한국어），根据 Windows 界面语言自动选择。
- 支持逐显示器 DPI、深色主题、GPU 计时和内置日志。

## 系统要求

| | |
|---|---|
| 系统 | Windows 10 21H2 / Windows 11，64 位 |
| 显卡 | NVIDIA GeForce RTX（DLSS 5 神经渲染仅支持 RTX 硬件），较新的 Game Ready 驱动 |
| VRChat | 任何带有 Stream 相机 *Spout Stream* 选项的版本（桌面或 VR） |
| DLSS 5 运行库 | 你自己的 `nvngx_dlssnr.dll`。**本项目不包含、也绝不会下载该文件。** |

## 使用步骤

1. 在 [Releases](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases) 页面下载 `VRChatDLSS5Cam-win64.zip` 并解压到任意位置。
2. 把你的 `nvngx_dlssnr.dll` 复制到解压目录（与 `VRChatDLSS5Cam.exe` 同级）。也可以在 *DLSS 5 神经渲染 → 运行库路径* 中指定文件。
3. 启动 VRChat，打开 **相机**，把相机模式切换到 **Stream**，并在 Stream 相机设置中开启 **Spout Stream**。VRChat 会把相机画面作为 Spout 发送端（`VRCSender1`）输出。
4. 启动 `VRChatDLSS5Cam.exe`。程序会自动连接发送端，预览区显示处理后的画面。
5. 在 VRChat 中取景，按 **Ctrl+Alt+P**（或点击 *拍照* 按钮）。PNG 默认保存到 `图片\VRChat DLSS5 Cam`。

提示
- Spout 输出分辨率由 VRChat 决定。可以在 VRChat 的 `config.json` 中提高 `camera_spout_res_width` / `camera_spout_res_height` 以获得更高分辨率的输入，程序会自动适配。
- 如果想要不同的输出尺寸，在 *视频源* 中开启 *自定义分辨率*；开启 *DLSS 5 放大* 后由 DLSSNR 直接渲染更大的图像。
- 使用 *擦除* 对比模式并拖动预览中的分割线，可以直观比较每个参数的效果。

## 参数说明

| 分组 | 设置 | 含义 |
|---|---|---|
| 源 | 纸白 / 高光压缩 | 仅在 Spout 纹理为浮点（线性 HDR）格式时显示：神经渲染前的曝光基准与高光柔化。 |
| DLSS 5 | 预设 | 传给 DLSSNR 的渲染预设提示（0–3）。 |
| DLSS 5 | 风格 | `DLSSNR.Style`：默认 / 自然 / 电影感。 |
| DLSS 5 | 强度 | 神经渲染的整体强度。 |
| DLSS 5 | 全局色调 / 局部色调 | 全局与局部色调映射强度。 |
| DLSS 5 | 局部结构 / 皮肤结构 | 细节增强；皮肤结构可保持运行库默认值。 |
| DLSS 5 | 自动遮罩 / UI 修正 | 自动主体遮罩、UI 安全处理。 |
| DLSS 5 | 路径 | *签名片段*：直接加载 `nvngx_dlssnr.dll`。*NGX 核心*：通过 NGX 运行库创建功能。 |
| 帧引导 | 运动矢量 | NVIDIA Optical Flow（驱动自带 `nvofapi64.dll`，带双向一致性检查）、GPU 块匹配，或无（零）。 |
| 帧引导 | 深度 | AI 估计（DirectML 上的 Depth Anything V2 Small，可调更新间隔和网络分辨率）、平面、渐变或零。 |
| 帧引导 | 自动重置 | 匹配代价突然跳变（镜头切换）时清空时域历史。默认关闭。 |
| DLAA | 启用 / 预设 | 在神经渲染前于原生分辨率执行的可选 DLSS 抗锯齿。 |
| 拍照 | 保留透明通道 / 同时保存原图 / 快捷键 / 定时连拍 | 拍照选项。 |
| 显示 | 对比 / 适应 / 垂直同步 / 叠加信息 | 预览选项。 |

设置保存在 `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini`，日志为同目录下的 `log.txt`。

## 常见问题

- **“等待 Spout 发送端”** —— 请在 VRChat 的 Stream 相机中开启 *Spout Stream*，且相机必须处于打开状态。其他 Spout 发送端会列在 *发送端* 下拉框中。
- **“未找到 nvngx_dlssnr.dll”** —— 把运行库复制到程序目录或选择其路径。
- **NGX 未初始化 / DLAA 不支持** —— NGX 运行库需要 NVIDIA 显卡和较新的驱动。DLSSNR 仍可通过 *签名片段* 路径工作。
- **程序打不开 / 一闪就退出** —— 打开 `%LOCALAPPDATA%\VRChatDLSS5Cam\`，查看 `log.txt`（最后一行就是失败的步骤）和 `crash.txt`（进程崩溃时写入）。提交 Issue 时请附上这两个文件。
- **神经渲染失败** —— 某些运行库版本需要更新的驱动；请查看 `log.txt` 中的 NGX 结果码。可尝试 *预设* 0 和 *NGX 核心* 路径。
- **深度估计器不可用** —— `onnxruntime.dll`、`onnxruntime_providers_shared.dll`、`DirectML.dll` 和 `models\depth_anything_v2_small_fp16.onnx` 必须放在程序目录下（发布包里都有）。估计器就绪之前程序会回退到零深度，状态显示在 *帧引导* 一栏。
- **光流不可用** —— NVIDIA Optical Flow 在程序自建的原生 D3D11 设备上运行（驱动会拒绝 D3D11On12 层，这正是 0.2.0 里 “UNSUPPORTED_DEVICE” 错误的原因）。若 `log.txt` 出现 "NVOF unavailable, falling back to block matching"，请更新 GeForce 驱动；在此之前程序会自动改用块匹配。*帧引导* 一栏的状态点会显示当前使用的来源。
- **帧率低** —— 关闭 DLAA、增大深度更新间隔或降低深度网络分辨率、降低搜索半径，或改用 NVIDIA Optical Flow 生成运动矢量。

## 从源码构建

需要 Visual Studio 2022（MSVC v143，Windows 10 SDK）和 CMake 3.21 及以上。

```powershell
git clone https://github.com/AlanBacker/VRChat-DLSS5-Cam.git
cd VRChat-DLSS5-Cam
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

配置阶段会从 NVIDIA 的公开 GitHub 仓库下载 NVIDIA DLSS SDK（头文件、`nvsdk_ngx_s.lib`、`nvngx_dlss.dll`），从 NuGet 下载
ONNX Runtime（DirectML 版）和 DirectML，并从 Hugging Face 下载 Depth Anything V2 Small FP16 模型（`-DVDC_FETCH_DEPTH_MODEL=OFF`
可跳过模型）。所有下载都会校验哈希。着色器在运行时编译，无需额外的着色器工具链。

## 工作原理

```
VRChat Stream 相机 ──Spout──▶ D3D11on12 接收 ──▶ 转换（sRGB / 缩放）
      ▶ NVIDIA Optical Flow（正向 + 反向）/ 块匹配 ──▶ 运动矢量 + 置信度
      ▶ Depth Anything V2（ONNX Runtime DirectML，每 N 帧一次）──▶ 归一化深度，其间用运动矢量重投影
      ▶ [DLAA] ──▶ DLSSNR（nvngx_dlssnr.dll）──▶ 合成 / 对比 ──▶ 预览 + PNG 拍照
```

程序在 NGX 运行库之外托管 DLSS 5 神经渲染片段：直接加载 DLL、满足其模块名检查，
并使用 `DLSSNR.*` NGX 参数约定在 D3D12 计算队列上创建和执行功能。详见 `src/ngx/DlssnrFeature.cpp`。

引导方案针对视频输入设计：同分辨率 SDR 输入；硬件光流，并在正反向矢量不一致处降低置信度；
用 Depth Anything V2 估计单目深度，按 2%/98% 分位数归一化为反向相对深度，推理间隔期间用运动矢量搬运；不做逐帧历史重置。
全部为独立的 MIT 实现（`src/gfx/Pipeline.cpp`、`src/gfx/DepthEstimator.cpp`、`src/gfx/Shaders.cpp`）。
光流引擎运行在程序自建的原生 D3D11 设备上，帧与矢量通过 NT 句柄共享纹理在 D3D11/D3D12 之间传递，并由共享 fence 保证顺序（`src/gfx/NvOpticalFlow.cpp`）。

## 许可证

MIT（见 `LICENSE`）。第三方组件及 NVIDIA 声明见 `THIRD_PARTY_NOTICES.md`。
本项目与 VRChat Inc. 或 NVIDIA Corporation 无关。DLSS 5 运行库为未发布软件，
使用风险自负，请勿将其与本程序一起分发。
