---
status: translated
title: 隐私与许可
nav: 隐私与许可
description: 程序会通过网络发送什么（几乎什么都不发），以及程序和各个组件的许可。
---
## 不上传任何内容 {#nothing-uploaded}

你的图片、视频和设置都留在你的电脑上。程序没有账号，不做跟踪，也不收集使用统计。所有处理都在你自己的显卡上运行。

## 程序什么时候联网 {#network}

程序只在下面这些情况下连接互联网：

| 内容 | 时机 | 连接到 |
|---|---|---|
| 检查更新 {#update-check} | 每次启动时（除非关闭了**启动时检查更新**），以及你点击**检查更新**时。 | GitHub，或你在 **GitHub 访问方式**中选择的镜像站。 |
| 下载更新 {#update-download} | 只在你点击**立即更新**，或运行 `--update` 或 `--edition` 时。 | 同上。 |
| 镜像站测速 {#mirror-sites} | 选择了**加速镜像站（自动测速选最快）**，或你点击**测速**时。 | 8 个已知的镜像站。 |
| DLSS-NR-on-AMD {#port} | 仅限 Radeon 版：查询它的最新版本，以及你点击按钮时下载安装器。 | 那个项目在 GitHub 上的发布页，或所选的镜像站。 |

用 `--headless` 或 `--process` 从命令行运行时，程序绝不会自行检查更新。

**文档**、**下载驱动**和**反馈问题**等按钮会在浏览器中打开一个网页。**反馈问题**会在 GitHub 的表单中填好程序版本和你的显卡；在你提交之前，不会发送任何内容。

## MCP 服务器 {#mcp}

在你打开之前，MCP 服务器一直是关闭的。打开后，它只监听你的电脑，绝不会主动连接任何地方。只有在你把**可访问范围**设为**局域网**并给出密钥之后，其他电脑才能访问它。[MCP](mcp.html)

## 本网站 {#this-site}

<!-- if askWidget -->
这些网页不会自行从其他服务器加载任何东西：没有字体，没有脚本，没有跟踪。唯一的例外是 [AI 问答](#ai-qa)，而且只在你要用它时才会加载。你选择的主题和语言只保存在你的浏览器中。

## AI 问答 {#ai-qa}

**问 AI** 会打开 AI 问答，它根据这些网页回答问题。提供它的是 [Mintlify](https://mintlify.com)，而不是本网站。

打开网页时，不会加载任何来自 Mintlify 的内容。只有当你把指针移到顶栏或搜索结果末尾的**问 AI** 上，或用键盘移到它上面时，才开始加载 Mintlify 的内容，好让你点击时它已经就绪。你输入的内容会发送给 Mintlify，由它写出回答，所以请不要写入任何私人信息。Mintlify 还会收到面板的使用事件，例如面板何时打开；本网站要求它不附带页面地址。为了拦住机器人，Mintlify 会用 hCaptcha 检查提问，面板会从 hCaptcha 的服务器加载这项检查。在当前浏览器标签页中，面板会保留对这次对话的引用，这样你接着追问时，会延续这次对话。
<!-- else -->
这些网页不从其他服务器加载任何东西：没有字体，没有脚本，没有跟踪。你选择的主题和语言只保存在你的浏览器中。
<!-- endif -->

## 许可 {#licence}

程序的许可声明，与 README 中的写法相同：

> MIT（见 `LICENSE`）。第三方组件和 NVIDIA 声明列在 `THIRD_PARTY_NOTICES.md`。本项目与 VRChat Inc. 或 NVIDIA Corporation 无关。

全文见 [LICENSE](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/LICENSE) 和 [THIRD_PARTY_NOTICES.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/THIRD_PARTY_NOTICES.md)。这两个文件也在程序的文件夹中，**关于** → **第三方声明**会打开它们。

## 程序使用的组件 {#components}

| 组件 | 许可 |
|---|---|
| NVIDIA DLSS 5 运行库（`nvngx_dlssnr.dll`） | NVIDIA 的软件，受 NVIDIA 的条款约束，不属于本程序的 MIT 许可范围。GeForce 版：两份；Radeon 版：一份，供 DLSS-NR-on-AMD 的安装器使用。 |
| NVIDIA DLSS SDK（NGX，`nvngx_dlss.dll`） | NVIDIA RTX SDKs License Agreement。 |
| NVIDIA Optical Flow SDK（接口头文件） | MIT。 |
| AMD FidelityFX SDK（`amd_fidelityfx_dx12.dll`，Radeon 版） | MIT。 |
| Spout2 | BSD 2-Clause。 |
| Dear ImGui | MIT。 |
| dlss5-bridge | MIT。 |
| ONNX Runtime | MIT。 |
| DirectML | Microsoft 的 DirectML 许可，允许随应用程序一起分发。 |
| Depth Anything V2 Small | Apache License 2.0。 |
| libwebp | BSD 3-Clause。 |
| Lucide 图标 | ISC；源自 Feather 的图标另适用 MIT。 |
| Direct3D 着色器编译器（`d3dcompiler_47.dll`，Linux 软件包） | Windows SDK 的 Microsoft 软件许可条款。 |

**DLSS-NR-on-AMD** 不包含在内。它是一个独立项目，有自己的条款：允许个人非商业使用，不允许再分发。只有在你要求时，Radeon 版才会从那个项目的发布页下载它的安装器。

本网站使用 ISC 许可的 Lucide 图标，许可全文见 [assets/licenses/lucide-LICENSE.txt]({root}assets/licenses/lucide-LICENSE.txt)。
