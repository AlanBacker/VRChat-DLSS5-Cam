# MCP server

VRChat DLSS5 Cam has an [MCP](https://modelcontextprotocol.io) (Model Context Protocol) server. An MCP client
(Claude Desktop, Claude Code, Cursor, a chat bot or another client) can open pictures and videos, change
every setting, run the library, save captures and look at the preview, through the same actions as the window.
Whatever the client does shows in the interface, enters the undo history and is saved like a change made by hand.

Other computers can send it work too: a bot (a chat-group bot, a script, a client on another PC) uploads a picture
or a video with a **key**, the file waits in a **queue**, and the bot is told its place and when its turn comes, then
downloads the result. One PC with a strong graphics card can serve several bots at once this way.

By default nothing leaves the computer: the server listens on `127.0.0.1` only, never connects anywhere, and refuses
requests from web pages (a browser sends an `Origin` header; only local origins pass).

## Turning it on

Sidebar, section **MCP**:

- **Run the MCP server** starts it, and it runs whenever the program does. The state line shows the address
  (`http://127.0.0.1:51550/mcp` by default) and how many calls came in.
- **Reach**: **This computer only** (the default; clients on this PC need no key) or **Local network** (other
  computers reach it at the addresses shown, each with a key from the list). **Allow through Windows Firewall** adds
  the rule the port needs; it asks for administrator rights once.
- **Port** changes the TCP port (1024 to 65535).
- **Read only** lets clients look (status, settings, log, preview) but change nothing; jobs do not run either.
- **Keys**: one row per key with its role, last use and call count, a button that copies the key again and one that
  removes it. Type a name (the bot's, the person's), choose a role, **Add key**: the key is copied to the clipboard
  and kept in `mcp-keys.json` in the settings folder.
- **Jobs**: how many wait, run and are kept; **Keep results** says for how many hours a finished job's files stay on
  disk for its client to download; **Open jobs folder** shows them.
- **Copy client configuration** puts the JSON block below on the clipboard; **Copy URL** copies the address;
  **Open in the browser** shows the server's information page with every tool, every setting, the queue and a
  ready-made bot script; **Documentation** opens this page.
- Behind the sidebar's **Advanced** switch: **Queue limit** (jobs that may wait at once, over all keys), **Per key**
  (jobs one key may have waiting or running), **Upload limit** (MB), **This computer needs no key** (off: local
  clients need a key too) and **Job folder** (where the clients' inputs and results are kept; empty = the settings
  folder's `mcp\`).

The same keys live in `settings.ini`: `mcpEnabled`, `mcpPort`, `mcpBind` (0 this computer, 1 local network),
`mcpReadOnly`, `mcpLocalNoKey`, `mcpKeepHours`, `mcpQueueMax`, `mcpQueuePerKey`, `mcpUploadMaxMb`, `mcpJobFolder`
and `mcpToken` (the older single token: when set, it is accepted as an admin key).

## Connecting a client on this computer

There are two ways in.

**The bridge** (works with every client, and the program does not have to be running): the client starts
`VRChatDLSS5Cam.exe --mcp`, which relays the client's messages to the program's server, starting the program when
none runs (with `--mcp-port` for that session, so the switch in the sidebar does not matter). Claude Desktop and
Cursor take this block in their MCP configuration file (the path is the installed executable's):

```json
{
  "mcpServers": {
    "vrchat-dlss5-cam": {
      "command": "C:\\Path\\To\\VRChatDLSS5Cam.exe",
      "args": ["--mcp"]
    }
  }
}
```

Claude Desktop: Settings → Developer → Edit Config (`claude_desktop_config.json`). Cursor: Settings → MCP → Add new
server (`~/.cursor/mcp.json`). Other options after `--mcp` travel to the program it starts, for example
`"args": ["--mcp", "--data-dir", "D:\\vdc-mcp"]` keeps the client's session apart from your own settings.

**Direct HTTP** (the program is running with the switch on): the server speaks MCP's Streamable HTTP transport at
`http://127.0.0.1:51550/mcp`. Claude Code:

```
claude mcp add --transport http vrchat-dlss5-cam http://127.0.0.1:51550/mcp
```

Every other client that takes a URL works the same way. The bridge log is `mcp-bridge.txt` next to `log.txt` in the
settings folder (`%LOCALAPPDATA%\VRChatDLSS5Cam`); it is rotated at 1 MB.

## Reaching the program from another computer

Step by step, on the PC that runs the program (the server):

1. Sidebar → **MCP** → **Run the MCP server** on.
2. **Reach** → **Local network**, then **Allow through Windows Firewall** (accept the administrator prompt).
3. **Keys** → type a name for the client (say `qq-bot`), role **Jobs**, **Add key**. The key is on the clipboard now:
   paste it somewhere safe and give it to that client. One key per bot or person, so a lost one can be removed alone.
4. Note the address under the state line, `http://192.168.x.x:51550/mcp` (each of the PC's network adapters is
   listed; use the one the other computer sees).

On the other computer:

- A client that takes a URL and a header (Claude Code, scripts, bots): the URL above with
  `Authorization: Bearer <key>` on every request, for example
  `claude mcp add --transport http vrchat-dlss5-cam http://192.168.1.20:51550/mcp --header "Authorization: Bearer vdc_..."`.
- A client that only starts a command (Claude Desktop, Cursor): the program's bridge relays to a remote server too.
  Copy `VRChatDLSS5Cam.exe` to that computer (nothing else is needed for the bridge) and configure
  `"args": ["--mcp", "--mcp-url", "http://192.168.1.20:51550/mcp", "--mcp-key", "vdc_..."]`.
- A browser: `http://192.168.1.20:51550/?key=vdc_...` shows the information page, the queue and a Python bot
  example filled in with that address and key.

The roles:

| Role | May |
| --- | --- |
| **Viewer** | Look: status, settings, log, preview, the library list, the history. |
| **Jobs** | Send files with `vdc_upload` and `vdc_submit`, follow and fetch its own jobs with `vdc_jobs`, see a reduced `vdc_get_status` (the machine and the queue, not the user's files), read `vdc_describe_settings` and the preset names. Nothing else: it cannot touch the window, the library or the settings. |
| **Admin** | Everything the window can do, including every key's jobs and files on the server itself (`vdc_submit path=`). |

Clients on the server PC itself need no key while **This computer needs no key** is on (they are admin then); the
older `mcpToken` also counts as an admin key. Keep the port on the local network or a VPN: the server speaks plain
HTTP, and a key is all a client needs.

## Jobs and the queue

A job is one file processed with the server's current settings (plus a preset or a few values of its own) into the
job's folder. Jobs from all keys line up in one queue and run one at a time, on the graphics card, after the user's
own work: a batch or a video run by the person at the window goes first, and a job waits for it.

What a bot does, and what it hears back:

1. **Send the file**: `POST /upload?name=cat.png` with the bytes as the body (any size up to the upload limit;
   `Authorization: Bearer <key>`), which answers `{"uploadId": "..."}`. Small files may go straight into `vdc_submit` as
   base64 `data` (up to 16 MB), or as a `url` the server downloads (an image link in a chat message), or, with an
   admin key, as a `path` on the server.
2. **`vdc_submit`** answers **at once**, without waiting for the run:
   `{"jobId": "...", "state": "queued", "position": 3, "ahead": 2, "etaSeconds": 40, "estimateSeconds": 6,
   "message": "Queued at position 3 (2 ahead), about 40 s until it starts. The run itself takes about 6 s."}`.
   The bot can tell its user that sentence and come back later; nobody has to hold a connection open. When the
   server has no room, `vdc_submit` fails with a reason the bot can act on: `queue_full` (with `etaSeconds` and
   `retryAfterSeconds`) or `too_many_jobs` (this key's share is used up: wait for one to finish or cancel one).
3. **Follow it**: `vdc_jobs get` answers now; `vdc_jobs wait` (with `timeout`, at most 60 s) answers when the job ends or
   when the time is up, always with the current state (never an error), so a bot can poll in 30-second steps without
   an awkward silence. A `wait=true` on `vdc_submit` does the same for short jobs.
4. **Fetch the result**: `vdc_jobs result` answers with the job's `outputs` (name, URL, bytes) and, for a picture,
   an inline copy scaled to `max_edge` pixels so a client can look at it. The files come from
   `GET /download/<jobId>/<name>` (or `/0`, `/1` for the first, second output) with the same key; another key's
   job answers 403. The bot posts the picture or the link to its chat.
5. **Clean up**: finished jobs and their files stay for **Keep results** hours (24 by default) and are deleted then;
   `vdc_jobs delete` removes one earlier. Unused uploads go after two hours.

Fairness: the key whose last job started longest ago goes first, then by submission, so one bot that sends ten files
does not keep the others waiting for all ten. `vdc_jobs cancel` stops a job (a running one stops within a second).
The user at the window sees an **AI job** badge and the owner's name while a job runs, and can cancel it there.

Jobs survive a restart: the queue is kept in `jobs.json` in the job folder, a job that was running starts again,
and a job whose input went missing fails with that reason. The information page and `/jobs.json` show the queue at a
glance, and `vdc_jobs list` gives a key its own jobs (an admin key: everyone's).

A minimal bot in Python (the information page prints this with your address and key filled in):

```python
import requests, time
S = "http://192.168.1.20:51550"; H = {"Authorization": "Bearer vdc_..."}
def tool(name, **args):
    r = requests.post(S + "/mcp", json={"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                      "params": {"name": name, "arguments": args}}, headers=H).json()
    return r["result"].get("structuredContent"), r["result"].get("isError")
up = requests.post(S + "/upload?name=cat.png", data=open("cat.png", "rb").read(), headers=H).json()
job, err = tool("vdc_submit", upload_id=up["uploadId"])
print(job["message"])                      # "Queued at position 2 (1 ahead), about 12 s until it starts. ..."
while job["state"] in ("queued", "running"):
    job, err = tool("vdc_jobs", action="wait", id=job["jobId"], timeout=30)
if job["state"] == "done":
    png = requests.get(job["outputs"][0]["url"], headers=H).content
```

## Chat bots: the AstrBot plugin

A chat model sees the picture a person posts, but it cannot hand the picture's bytes to a tool: `vdc_submit` needs an
`upload_id`, a `url` or base64 `data`, and none of those exist for a picture that only sits in the chat. The
repository ships a plugin for [AstrBot](https://astrbot.app) that closes the gap: `integrations/astrbot/astrbot_plugin_vdc`
(also `astrbot_plugin_vdc.zip` with each release).

What it does:

- Before the model answers, every picture or video in the message (and in the message it replies to) is uploaded to
  the server, and the model is told "`cat.jpg`: upload_id ..." together with what to do: call `vdc_submit` with that
  id. The model never has to pass bytes or a path it does not have.
- The plugin watches the jobs made from its uploads (a job's `uploadId` names the upload it came from) and posts the
  finished picture or video into the chat the picture came from, by itself; the model only relays the queue message.
  Without that, a result would reach the model as an image and depend on the model to forward it.
- `/vdc` (sent with a picture, or as a reply to one) processes it without any model; `/vdc status` and `/vdc jobs`
  show the server and the queue. **Queue every uploaded picture at once** (`auto_submit`) makes the plugin submit by
  itself, so no MCP setup is needed at all. Two model tools of its own, `vdc_queue` and `vdc_post_result`, cover a
  model that has no MCP tools.

Setting it up:

1. In the program: **Run the MCP server**; **Reach: Local network** when AstrBot runs on another computer; **Add
   key** with the jobs role (it is copied); **Allow through Windows Firewall**.
2. In AstrBot's web interface: **Plugins**, install the zip (or copy the folder into `data/plugins`), then the
   plugin's settings: the server address as the sidebar shows it, without `/mcp` (`http://192.168.1.20:51550`), and
   the key.
3. For the model to drive the server too (change the look, apply a preset, read the queue), add the server under
   **MCP servers** with the same key:
   `{"url": "http://192.168.1.20:51550/mcp", "transport": "streamable_http", "headers": {"Authorization": "Bearer vdc_..."}}`.
   Use the same key in both places, or an admin key for the plugin, so the plugin sees the jobs the model submits.

The plugin needs nothing beyond AstrBot itself. Its settings are explained in its own `README.md`.

## What a client can do

Every tool is named `vdc_...`, so a client with several servers never mixes them up (the names without the
prefix, from v1.8.0, still answer).

| Tool | What it does |
| --- | --- |
| `vdc_get_status` | The state of the program: the source and what is loaded, the neural and guidance passes, the output size, a running batch or capture, the library, the undo history, the frame rates, the queue. A jobs key gets the machine, the neural pass and the queue only. |
| `vdc_describe_settings` | The settings reference: every key with its group, type, range or values and meaning (all, a `group`, or one `key`). |
| `vdc_get_settings` | The current values (all, or `keys`). |
| `vdc_set_settings` | Changes settings, as the sidebar does: `{"settings": {"nrIntensity": 1.5, "compareMode": 2}}`. The answer says what was applied, unchanged, unknown or refused. |
| `vdc_reset_settings` | Every setting back to its default (the About section's button). |
| `vdc_open` | Opens a picture or video (`path`), adds it to the library, waits until it is loaded and shown. |
| `vdc_open_live` | Shows the live stream (Spout), optionally from a named `sender`. |
| `vdc_list_senders` | The Spout senders on this computer. |
| `vdc_close_media` | Closes the opened file (back to the live view). |
| `vdc_library` | The media library: `list`, `add` (`paths`: files or folders), `remove`, `clear`, `select`, `show`, `set_range` (video in/out), `set_own` / `clear_own` (a file's own effect values). |
| `vdc_process` | Processes the library (`scope`: `all`, `selected` or `ids`), optionally waiting for the end (`wait`, `timeout`). |
| `vdc_capture` | Saves the opened picture, processes the opened video (its range), or takes a photo of the live stream; waits by default. |
| `vdc_cancel` | Stops a running video or batch. |
| `vdc_video` | The video controls: `info`, `play`, `pause`, `toggle`, `seek` (`seconds`), `step` (`frames`), `set_in`, `set_out`, `clear_range`. |
| `vdc_preview` | A PNG of the preview: `view` = `output` (the picture as shown, including the comparison mode and the wipe) or `window` (the whole window), scaled to `max_edge` pixels (64 to 4096), optionally also saved full-size to `save_to`. |
| `vdc_wait` | Waits `for` = `idle` (no run, no pending capture), `loaded` (the opened file), `converged` (a still picture's passes are done) or `display` (a picture is on screen). |
| `vdc_undo` / `vdc_redo` | Ctrl+Z / Ctrl+Y, `steps` at a time. |
| `vdc_history` | The undo history with labels; `index` goes to that state. |
| `vdc_presets` | The user's presets: `list`, `apply`, `save`, `delete`, `rename`. |
| `vdc_get_log` | The last `lines` of the log, optionally from `level` up or those that contain a text. |
| `vdc_window` | `show`, `restore`, `minimize`, `fullscreen`, `windowed`, `resize` (`width`, `height`), `sidebar`, `library` (`visible`). |
| `vdc_reload` | `runtime` (the DLSS 5 runtime), `depth` (the depth estimator), `senders`, `history` (the temporal history of the neural pass). |
| `vdc_upload` | A file for a job as base64 `data` with its `name` (up to 16 MB; larger files go to `POST /upload`). Answers an `uploadId` good for two hours. |
| `vdc_submit` | A job: the input as `upload_id`, `data` (base64, with `name`), `url` or `path` (admin); `preset` (a preset's name, or `default` for the program's default look), `settings` (the job's own values: the look, the output and the guidance keys), `in` / `out` (a video's range in seconds), `label`, `client_ref` (the bot's own note, say the chat and the user), `vdc_wait` / `timeout`. Answers the job with its position and time estimate at once. |
| `vdc_jobs` | `list` (this key's jobs; all with an admin key), `get`, `wait` (`timeout` up to 60 s), `result` (with the inline picture, `inline`, `max_edge`), `vdc_cancel`, `delete`; `id` names the job. |

The same things are also resources (`vdc://status`, `vdc://settings`, `vdc://settings/schema`, `vdc://library`,
`vdc://log`, `vdc://preview`, `vdc://preview/window`, `vdc://jobs`) and plain HTTP pages for scripts and browsers:
`/status.json`, `/settings.json`, `/library.json`, `/jobs.json`, `/log.txt`, `/preview.png?max_edge=800`,
`/window.png`, `POST /upload?name=`, `GET /download/<job>/<file>`, and `/` for the information page. A key goes in
`Authorization: Bearer <key>` or `?key=<key>`.

A typical session: `vdc_get_status` → `vdc_open` a picture → `vdc_describe_settings group=neural` → `vdc_set_settings` →
`vdc_wait for=converged` → `vdc_preview` → adjust → `vdc_capture`. For many files: `vdc_library add` with a folder → `vdc_set_settings` →
`vdc_process wait=true`. For a video: `vdc_open` → `vdc_video set_in` / `set_out` → `vdc_capture wait=true`. For a bot on another
computer: `vdc_upload` → `vdc_submit` → `vdc_jobs wait` → `vdc_jobs result`.

## Running the program as a server

`VRChatDLSS5Cam.exe --headless` with **Run the MCP server** on (or `--set mcpEnabled=1 --set mcpBind=1`) runs
without a window until it is closed (`--exit-after <seconds>` ends it), serves the queue and writes `log.txt` as
usual; a Task Scheduler entry "at log-on" makes it come back with the PC. The graphics card still needs a logged-on
desktop session for the neural pass (the DLSS runtime does not run in session 0), so keep the PC logged in, with
automatic log-on or a remote desktop tool that keeps the session, and let the program start there.

## Notes

- Calls run on the interface thread between the window's own draw and its events, one after the other, so a
  client and a person can use the program at the same time; what the client changes is undoable with Ctrl+Z.
- A minimized window draws nothing: `vdc_preview` asks for `vdc_window restore` first. Everything else works while
  minimized, and in a `--headless` run (`vdc_preview view=output` works there too; `vdc_window` is a no-op).
- `vdc_set_settings` refuses `imagePath` and `videoPath` (use `vdc_open`) and the MCP server's own keys (`mcp*`: the user
  sets those up in the sidebar), and answers `unchanged` for a value that the range clamp brought back, so read the
  answer. `vdc_reset_settings` keeps them too.
- A running batch, video or job locks the library and the file tools, as it locks the window; `vdc_cancel` ends the
  user's own run, `vdc_jobs cancel` a job.
- Timeouts: a tool that waits (`vdc_open`, `vdc_capture`, `vdc_process`, `vdc_wait`, `vdc_jobs wait`) has a `timeout` in seconds and
  answers with the state reached so far when it runs out; `vdc_get_log` tells why something failed.
- Too many clients at once: the server takes 64 connections and 32 calls in flight; beyond that it answers 503 /
  `busy` with a retry time instead of queueing silently.
