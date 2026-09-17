# AI assistant (MCP server)

VRChat DLSS5 Cam has an [MCP](https://modelcontextprotocol.io) (Model Context Protocol) server. An AI assistant that
speaks MCP (Claude Desktop, Claude Code, Cursor, and the other clients) can open pictures and videos, change every
setting, run the library, save captures and look at the preview, through the same actions as the window. Whatever the
assistant does shows in the interface, enters the undo history and is saved like a change made by hand.

Nothing leaves the computer. The server listens on `127.0.0.1` only, never connects anywhere, and refuses requests
from web pages (a browser sends an `Origin` header; only local origins pass).

## Turning it on

Sidebar, section **AI assistant (MCP)**:

- **Run the MCP server** starts it, and it runs whenever the program does. The state line shows the address
  (`http://127.0.0.1:51550/mcp` by default) and how many calls came in.
- **Port** changes the TCP port (1024 to 65535).
- **Read only** lets the assistant look (status, settings, log, preview) but change nothing.
- **Copy client configuration** puts the JSON block below on the clipboard; **Copy URL** copies the address;
  **Open in the browser** shows the server's information page with every tool and every setting.

The same keys live in `settings.ini`: `mcpEnabled`, `mcpPort`, `mcpReadOnly` and `mcpToken` (when set, every request
must carry it as `Authorization: Bearer <token>` or `?token=<token>`; the interface does not edit it).

## Connecting a client

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
`"args": ["--mcp", "--data-dir", "D:\\vdc-assistant"]` keeps the assistant's session apart from your own settings.

**Direct HTTP** (the program is running with the switch on): the server speaks MCP's Streamable HTTP transport at
`http://127.0.0.1:51550/mcp`. Claude Code:

```
claude mcp add --transport http vrchat-dlss5-cam http://127.0.0.1:51550/mcp
```

Every other client that takes a URL works the same way. The bridge log is `mcp-bridge.txt` next to `log.txt` in the
settings folder (`%LOCALAPPDATA%\VRChatDLSS5Cam`).

## What the assistant can do

| Tool | What it does |
| --- | --- |
| `get_status` | The state of the program: the source and what is loaded, the neural and guidance passes, the output size, a running batch or capture, the library, the undo history, the frame rates. |
| `describe_settings` | The settings reference: every key with its group, type, range or values and meaning (all, a `group`, or one `key`). |
| `get_settings` | The current values (all, or `keys`). |
| `set_settings` | Changes settings, as the sidebar does: `{"settings": {"nrIntensity": 1.5, "compareMode": 2}}`. The answer says what was applied, unchanged, unknown or refused. |
| `reset_settings` | Every setting back to its default (the About section's button). |
| `open` | Opens a picture or video (`path`), adds it to the library, waits until it is loaded and shown. |
| `open_live` | Shows the live stream (Spout), optionally from a named `sender`. |
| `list_senders` | The Spout senders on this computer. |
| `close_media` | Closes the opened file (back to the live view). |
| `library` | The media library: `list`, `add` (`paths`: files or folders), `remove`, `clear`, `select`, `show`, `set_range` (video in/out), `set_own` / `clear_own` (a file's own effect values). |
| `process` | Processes the library (`scope`: `all`, `selected` or `ids`), optionally waiting for the end (`wait`, `timeout`). |
| `capture` | Saves the opened picture, processes the opened video (its range), or takes a photo of the live stream; waits by default. |
| `cancel` | Stops a running video or batch. |
| `video` | The video controls: `info`, `play`, `pause`, `toggle`, `seek` (`seconds`), `step` (`frames`), `set_in`, `set_out`, `clear_range`. |
| `preview` | A PNG of the preview: `view` = `output` (the picture as shown, including the comparison mode and the wipe) or `window` (the whole window), scaled to `max_edge` pixels (64 to 4096), optionally also saved full-size to `save_to`. |
| `wait` | Waits `for` = `idle` (no run, no pending capture), `loaded` (the opened file), `converged` (a still picture's passes are done) or `display` (a picture is on screen). |
| `undo` / `redo` | Ctrl+Z / Ctrl+Y, `steps` at a time. |
| `history` | The undo history with labels; `index` goes to that state. |
| `presets` | The user's presets: `list`, `apply`, `save`, `delete`, `rename`. |
| `get_log` | The last `lines` of the log, optionally from `level` up or those that contain a text. |
| `window` | `show`, `restore`, `minimize`, `fullscreen`, `windowed`, `resize` (`width`, `height`), `sidebar`, `library` (`visible`). |
| `reload` | `runtime` (the DLSS 5 runtime), `depth` (the depth estimator), `senders`, `history` (the temporal history of the neural pass). |

The same things are also resources (`vdc://status`, `vdc://settings`, `vdc://settings/schema`, `vdc://library`,
`vdc://log`, `vdc://preview`, `vdc://preview/window`) and plain HTTP pages for scripts and browsers:
`/status.json`, `/settings.json`, `/library.json`, `/log.txt`, `/preview.png?max_edge=800`, `/window.png`, and `/`
for the information page.

A typical session: `get_status` → `open` a picture → `describe_settings group=neural` → `set_settings` →
`wait for=converged` → `preview` → adjust → `capture`. For many files: `library add` with a folder → `set_settings` →
`process wait=true`. For a video: `open` → `video set_in` / `set_out` → `capture wait=true`.

## Notes

- Calls run on the interface thread between the window's own draw and its events, one after the other, so an
  assistant and a person can use the program at the same time; what the assistant changes is undoable with Ctrl+Z.
- A minimized window draws nothing: `preview` asks for `window restore` first. Everything else works while
  minimized, and in a `--headless` run (`preview view=output` works there too; `window` is a no-op).
- `set_settings` refuses `imagePath` and `videoPath` (use `open`) and the MCP server's own keys (`mcpEnabled`,
  `mcpPort`, `mcpReadOnly`, `mcpToken`: the user sets those up in the sidebar), and answers `unchanged` for a value
  that the range clamp brought back, so read the answer. `reset_settings` keeps them too.
- A running batch or video locks the library and the file tools, as it locks the window; `cancel` ends it.
- Timeouts: a tool that waits (`open`, `capture`, `process`, `wait`) has a `timeout` in seconds and answers with the
  state reached so far when it runs out; `get_log` tells why something failed.
