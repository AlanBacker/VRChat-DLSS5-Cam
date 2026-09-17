# VRChat DLSS5 Cam plugin for AstrBot

[中文说明在下方](#中文说明)

[VRChat DLSS5 Cam](https://github.com/AlanBacker/VRChat-DLSS5-Cam) applies NVIDIA DLSS 5 neural rendering to pictures
and videos. Its MCP server (v1.8.1 or newer) takes jobs from other computers. This plugin connects an
[AstrBot](https://astrbot.app) chat bot to such a server, so that people in a QQ group, on Telegram, Discord or any
other platform AstrBot speaks can post a picture and get the processed picture back.

## Why a plugin

A chat model sees the picture a person posts, but it cannot hand the picture's bytes to a tool: the server's
`vdc_submit` wants an `upload_id`, a `url` or base64 `data`, and the model has none of them. The plugin does three things:

1. **Before the model answers**, every picture or video in the message (and in the message it replies to) is uploaded
   to the server, and the model is told each upload id and what to do with it: call `vdc_submit` (or the plugin's
   own `vdc_queue`) with that id.
2. **The results come back by themselves.** The plugin watches the jobs made from its uploads and posts the finished
   picture or video into the chat it came from; the model only relays the queue message ("queued at position 2, about
   40 s").
3. **`/vdc`** processes the pictures of a message without any model: `/vdc` (with a picture, or as a reply to one),
   `/vdc Natural` (a preset by name), `/vdc status`, `/vdc jobs`.

## Setting it up

In VRChat DLSS5 Cam, sidebar section **MCP**:

- **Run the MCP server** on.
- **Reach: Local network** when AstrBot runs on another computer (**Allow through Windows Firewall** opens the port).
- **Add key**: a name (the bot's) and the **jobs** role; the key is copied to the clipboard.

In AstrBot's web interface:

- **Plugins**: install `astrbot_plugin_vdc.zip` (from the release page), or copy this folder into AstrBot's `data/plugins`.
- The plugin's settings: **Server address** as the sidebar shows it, without `/mcp` (for example
  `http://192.168.1.20:51550`), and the **Key**.
- Optional, for the model to drive the server too (change the look, apply presets, read the queue): **MCP servers**,
  add one with the same key:

  ```json
  {"url": "http://192.168.1.20:51550/mcp", "transport": "streamable_http", "headers": {"Authorization": "Bearer vdc_..."}}
  ```

  Use the same key in both places, or give the plugin an admin key, so the plugin sees the jobs the model submits.

Then post a picture with the bot's wake word ("@bot make this nicer") or with `/vdc`.

## Settings

| Setting | Meaning |
| --- | --- |
| Server address | `http://<host>:<port>` of the server (the sidebar shows it). |
| Key | A key with the jobs or admin role. Empty works only on the server PC while "This computer needs no key" is on. |
| Upload the pictures people post | Upload before the model answers and tell the model the ids (on by default). |
| Post finished results into the chat by itself | Watch the jobs and post their results (on by default). Off: the model calls `vdc_post_result`. |
| Queue every uploaded picture at once | The plugin submits each upload itself with the preset below; no MCP setup is needed then, the model only relays the queue message. Off by default: the model decides. |
| Preset | For `/vdc` and queued pictures: a preset saved in the program, or `default` for its default look. |
| Text posted with a result | Posted before the picture; empty posts the picture alone. |
| Seconds between two looks at the queue | How often the plugin asks the server about watched jobs (3). |
| Largest file to upload (MB) | Larger files are left alone (64). |
| HTTP timeout (seconds) | For one upload or download (120). |

## Notes

- Results are downloaded into AstrBot's `data/plugin_data/astrbot_plugin_vdc` and deleted after an hour.
- Uploads the model never submits are dropped by the server after two hours.
- The plugin uses only AstrBot's own dependencies (aiohttp). MIT licence.

---

## 中文说明

[VRChat DLSS5 Cam](https://github.com/AlanBacker/VRChat-DLSS5-Cam) 用 NVIDIA DLSS 5 神经渲染处理图片和视频，它的 MCP
服务器（1.8.1 起）能接收其他电脑交来的任务。这个插件把 [AstrBot](https://astrbot.app) 机器人接到这样一台服务器上：QQ 群、Telegram、
Discord 等 AstrBot 支持的平台里，有人发一张图，就能收到处理后的图。

### 为什么需要插件

模型能"看到"群里发的图片，却没法把图片本身交给工具：服务器的 `vdc_submit` 需要 `upload_id`、`url` 或 base64 的 `data`，模型一个都没有。插件做三件事：

1. **模型回答之前**，把消息里（以及被回复的那条消息里）的每张图片、每个视频先上传到服务器，并告诉模型每个上传编号该怎么用：带着这个编号调用
   `vdc_submit`（或插件自带的 `vdc_queue`）。
2. **结果自动发回。** 插件盯着由它上传的文件生成的任务，处理完成后把图片或视频直接发回原来的群或私聊；模型只需转述排队信息（"排在第 2 位，大约 40 秒"）。
3. **`/vdc` 指令**不经过模型直接处理：`/vdc`（带图发送，或回复一张图）、`/vdc Natural`（指定预设名）、`/vdc status`、`/vdc jobs`。

### 配置步骤

VRChat DLSS5 Cam 侧栏 **MCP** 一节：

- 打开 **运行 MCP 服务器**。
- AstrBot 在另一台电脑上时，**可访问范围** 选 **局域网**（**允许通过 Windows 防火墙** 会放行端口）。
- **添加密钥**：填名字（机器人的名字），角色选 **任务**，密钥会复制到剪贴板。

AstrBot 网页后台：

- **插件**：安装 Release 页面附带的 `astrbot_plugin_vdc.zip`，或把本文件夹复制到 AstrBot 的 `data/plugins` 里。
- 插件设置：**服务器地址** 按侧栏显示的填，不带 `/mcp`（例如 `http://192.168.1.20:51550`），再填 **密钥**。
- 可选，想让模型也能操作服务器（调整效果、套用预设、查看队列）：在 **MCP 服务器** 里用同一把密钥添加：

  ```json
  {"url": "http://192.168.1.20:51550/mcp", "transport": "streamable_http", "headers": {"Authorization": "Bearer vdc_..."}}
  ```

  两处用同一把密钥，或者给插件一把管理密钥，插件才能看到模型提交的任务。

然后带着唤醒词发图（"@机器人 把这张图处理一下"），或者用 `/vdc`。

### 设置项

| 设置 | 含义 |
| --- | --- |
| 服务器地址 | 服务器的 `http://<主机>:<端口>`（侧栏里有）。 |
| 密钥 | 任务或管理角色的密钥。留空只在服务器本机、且开着"本机无需密钥"时可用。 |
| 模型回答前先上传图片 | 先上传并把编号告诉模型（默认开）。 |
| 自动把结果发回聊天 | 盯住任务并自动发回结果（默认开）。关掉则由模型调用 `vdc_post_result`。 |
| 上传后立刻排队 | 插件自己用下面的预设提交每个上传，无需配置 MCP，模型只转述排队信息。默认关：由模型决定。 |
| 预设 | `/vdc` 和自动排队用的预设：程序里保存的预设名，或 `default` 表示默认效果。 |
| 结果附带的文字 | 发在图片前面；留空只发图。 |
| 查询队列的间隔（秒） | 插件多久问一次服务器（3）。 |
| 最大上传体积（MB） | 更大的文件不处理（64）。 |
| HTTP 超时（秒） | 单次上传或下载的时限（120）。 |

### 备注

- 结果先下载到 AstrBot 的 `data/plugin_data/astrbot_plugin_vdc`，一小时后删除。
- 模型没有提交的上传，服务器两小时后自动清理。
- 插件只用 AstrBot 自带的依赖（aiohttp）。MIT 许可。
