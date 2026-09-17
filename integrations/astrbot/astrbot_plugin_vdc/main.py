"""VRChat DLSS5 Cam bridge for AstrBot.

A chat model sees the picture a person posts, but it cannot hand the picture's bytes to a tool. This plugin closes
that gap for a VRChat DLSS5 Cam server (its MCP server, from v1.8.1):

- before the model answers, every picture or video in the message is uploaded to the server, and the model is told
  the upload id, so it only has to call vdc_submit with that id (or the plugin queues the file itself);
- the plugin watches the jobs made from those uploads and posts the finished result into the chat they came from;
- /vdc processes the pictures of a message without any model.

MIT licence, like the rest of the repository.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import os
import re
import time
from typing import Any
from urllib.parse import quote

import aiohttp

import astrbot.api.message_components as Comp
from astrbot.api import AstrBotConfig, logger
from astrbot.api.event import AstrMessageEvent, MessageChain, filter
from astrbot.api.provider import ProviderRequest
from astrbot.api.star import Context, Star

try:
    from astrbot.core.utils.astrbot_path import get_astrbot_plugin_data_path
except Exception:  # an older AstrBot
    get_astrbot_plugin_data_path = None

PICTURE_EXT = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff", ".gif", ".apng"}
VIDEO_EXT = {".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v"}
UPLOAD_KEEP = 2 * 3600       # the server drops an unused upload after two hours
JOB_KEEP = 24 * 3600         # a watched job is forgotten after a day
FILE_KEEP = 3600             # a posted result file is deleted after an hour
FINAL_STATES = ("done", "failed", "cancelled")


class VdcError(Exception):
    pass


def sniff_ext(data: bytes) -> str:
    """The file type from the first bytes, for pictures that arrive with no usable extension."""
    if data.startswith(b"\x89PNG"):
        return ".png"
    if data.startswith(b"\xff\xd8"):
        return ".jpg"
    if data.startswith(b"GIF8"):
        return ".gif"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return ".webp"
    if data.startswith(b"BM"):
        return ".bmp"
    if data[4:8] == b"ftyp":
        return ".mp4"
    if data.startswith(b"\x1a\x45\xdf\xa3"):
        return ".mkv"
    return ""


def clean_name(name: str, data: bytes) -> str:
    """A file name the server accepts: ASCII-safe, with an extension that says the format."""
    base = os.path.basename(name or "").strip() or "picture"
    base = re.sub(r"[^A-Za-z0-9._-]+", "_", base)[:80]
    root, ext = os.path.splitext(base)
    ext = ext.lower()
    if ext not in PICTURE_EXT and ext not in VIDEO_EXT:
        ext = sniff_ext(data) or ".png"
    return (root or "picture") + ext


class VdcServer:
    """The server's HTTP side: uploads, tool calls and downloads."""

    def __init__(self, url: str, key: str, timeout: int):
        base = (url or "").strip().rstrip("/")
        if base.endswith("/mcp"):
            base = base[:-4]
        if base and not base.startswith("http"):
            base = "http://" + base
        self.base = base
        self.key = (key or "").strip()
        self.timeout = max(10, int(timeout or 120))
        self._session: aiohttp.ClientSession | None = None

    @property
    def headers(self) -> dict[str, str]:
        h = {"Accept": "application/json"}
        if self.key:
            h["Authorization"] = "Bearer " + self.key
        return h

    async def session(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=self.timeout))
        return self._session

    async def close(self) -> None:
        if self._session and not self._session.closed:
            await self._session.close()

    @staticmethod
    async def _error_text(resp: aiohttp.ClientResponse) -> str:
        try:
            body = await resp.json(content_type=None)
            if isinstance(body, dict) and body.get("error"):
                return str(body["error"])
        except Exception:
            pass
        return f"HTTP {resp.status}"

    async def upload(self, name: str, data: bytes) -> dict[str, Any]:
        s = await self.session()
        async with s.post(self.base + "/upload?name=" + quote(name), headers=self.headers, data=data) as resp:
            if resp.status != 200:
                raise VdcError(await self._error_text(resp))
            return await resp.json(content_type=None)

    async def call(self, tool: str, **args: Any) -> dict[str, Any]:
        """One MCP tool call; answers with the tool's structured result or raises VdcError with its message."""
        s = await self.session()
        body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": tool, "arguments": args}}
        async with s.post(self.base + "/mcp", headers=self.headers, json=body) as resp:
            if resp.status != 200:
                raise VdcError(await self._error_text(resp))
            r = await resp.json(content_type=None)
        if "error" in r:
            raise VdcError(str(r["error"].get("message", r["error"])))
        result = r.get("result") or {}
        if result.get("isError"):
            texts = [c.get("text", "") for c in result.get("content", []) if c.get("type") == "text"]
            raise VdcError(" ".join(texts) or "the tool failed")
        return result.get("structuredContent") or {}

    async def download(self, job_id: str, name: str) -> bytes:
        s = await self.session()
        async with s.get(self.base + "/download/" + quote(job_id) + "/" + quote(name), headers=self.headers) as resp:
            if resp.status != 200:
                raise VdcError(await self._error_text(resp))
            return await resp.read()


class VdcPlugin(Star):
    def __init__(self, context: Context, config: AstrBotConfig | None = None):
        super().__init__(context)
        self.config = config if config is not None else {}
        self._server: VdcServer | None = None
        self._server_id: tuple = ()
        self.watch: dict[str, dict[str, Any]] = {}   # upload id -> where the result goes
        self.jobs: dict[str, dict[str, Any]] = {}    # job id -> where the result goes
        self.done: set[str] = set()                  # jobs already posted
        self._task: asyncio.Task | None = None
        self.dir = os.path.join(get_astrbot_plugin_data_path() if get_astrbot_plugin_data_path else os.getcwd(), "astrbot_plugin_vdc")
        os.makedirs(self.dir, exist_ok=True)
        self._start_poller()

    async def terminate(self):
        if self._task:
            self._task.cancel()
            self._task = None
        if self._server:
            await self._server.close()

    # --- configuration ----------------------------------------------------------------------------

    def cfg(self, key: str, default: Any = None) -> Any:
        try:
            v = self.config.get(key, default)
        except Exception:
            v = default
        return default if v is None else v

    def server(self) -> VdcServer:
        ident = (self.cfg("url", ""), self.cfg("key", ""), self.cfg("timeout_seconds", 120))
        if self._server is None or ident != self._server_id:
            old = self._server
            self._server = VdcServer(*ident)
            self._server_id = ident
            if old:
                asyncio.ensure_future(old.close())
        return self._server

    def configured(self) -> bool:
        return bool(self.cfg("url", ""))

    def _start_poller(self) -> None:
        if self._task and not self._task.done():
            return
        try:
            self._task = asyncio.get_running_loop().create_task(self._poll())
        except RuntimeError:
            self._task = None   # no loop yet: the first message starts it

    # --- the files people post ----------------------------------------------------------------------

    async def collect(self, event: AstrMessageEvent, req: ProviderRequest | None = None) -> list[tuple[str, bytes]]:
        """The pictures and videos of the message (and of the message it replies to), as (name, bytes)."""
        limit = max(1, int(self.cfg("max_upload_mb", 64))) << 20
        comps: list[Any] = []
        try:
            comps = list(event.message_obj.message or [])
        except Exception:
            pass
        for c in list(comps):
            if isinstance(c, Comp.Reply) and getattr(c, "chain", None):
                comps.extend(c.chain)
        files: list[tuple[str, bytes]] = []
        seen: set[str] = set()

        def keep(name: str, data: bytes) -> None:
            if not data:
                return
            if len(data) > limit:
                logger.info(f"[vdc] {name} is larger than {limit >> 20} MB: left alone")
                return
            h = hashlib.sha1(data).hexdigest()
            if h in seen:
                return
            seen.add(h)
            files.append((clean_name(name, data), data))

        for c in comps:
            try:
                if isinstance(c, Comp.Image):
                    path = await c.convert_to_file_path()
                    keep(os.path.basename(path), _read(path))
                elif isinstance(c, Comp.Video):
                    path = await c.convert_to_file_path()
                    keep(os.path.basename(path), _read(path))
                elif isinstance(c, Comp.File):
                    path = await c.get_file()
                    if path and os.path.exists(path):
                        keep(c.name or os.path.basename(path), _read(path))
            except Exception as e:
                logger.warning(f"[vdc] a file of the message could not be read: {e}")
        if not files and req is not None:
            for i, s in enumerate(getattr(req, "image_urls", None) or []):
                try:
                    data = await self._bytes_of(s)
                    keep(f"picture_{i + 1}", data)
                except Exception as e:
                    logger.warning(f"[vdc] picture {i + 1} of the request could not be read: {e}")
        return files

    async def _bytes_of(self, s: str) -> bytes:
        """A request's image entry: a base64 string, a data URL, a file path or an http URL."""
        if not s:
            return b""
        if s.startswith("base64://"):
            return base64.b64decode(s[9:])
        if s.startswith("data:"):
            return base64.b64decode(s.split(",", 1)[1])
        if s.startswith("http://") or s.startswith("https://"):
            sess = await self.server().session()
            async with sess.get(s) as resp:
                return await resp.read() if resp.status == 200 else b""
        if s.startswith("file://"):
            return _read(s[7:])
        if os.path.exists(s):
            return _read(s)
        return base64.b64decode(s)

    # --- the bridge -----------------------------------------------------------------------------------

    def target(self, event: AstrMessageEvent, name: str) -> dict[str, Any]:
        return {"umo": event.unified_msg_origin, "name": name, "at": time.time()}

    async def queue(self, upload_id: str, name: str, event: AstrMessageEvent, preset: str | None = None) -> dict[str, Any]:
        """Submits an upload and remembers where its result goes."""
        args: dict[str, Any] = {"upload_id": upload_id, "label": name, "client_ref": event.unified_msg_origin[:200]}
        preset = (preset or self.cfg("preset", "default") or "").strip()
        if preset:
            args["preset"] = preset
        job = await self.server().call("vdc_submit", **args)
        jid = job.get("jobId")
        if jid:
            self.jobs[jid] = self.target(event, name)
            self._start_poller()
        return job

    @filter.on_llm_request()
    async def on_llm_request(self, event: AstrMessageEvent, req: ProviderRequest):
        """Uploads the message's pictures before the model answers, and tells the model their upload ids."""
        if not self.cfg("upload_pictures", True) or not self.configured():
            return
        files = await self.collect(event, req)
        if not files:
            return
        self._start_poller()
        auto = bool(self.cfg("auto_submit", False))
        lines = ["[VRChat DLSS5 Cam] The person's message carries these files, already uploaded to the server:"]
        for name, data in files:
            try:
                up = await self.server().upload(name, data)
            except Exception as e:
                logger.warning(f"[vdc] the upload of {name} failed: {e}")
                lines.append(f"- {name}: the upload failed ({e})")
                continue
            uid = str(up.get("uploadId", ""))
            self.watch[uid] = self.target(event, name)
            if auto:
                try:
                    job = await self.queue(uid, name, event)
                    lines.append(f"- {name}: already queued as job {job.get('jobId')}. {job.get('message', '')}")
                except Exception as e:
                    lines.append(f"- {name}: upload_id {uid}; queueing it failed: {e}")
            else:
                lines.append(f"- {name}: upload_id {uid}")
        if auto:
            lines.append("They are queued already: tell the person so, with the queue message, and stop. Do not submit them again.")
        else:
            lines.append("To process one, call vdc_submit with its upload_id (preset \"default\" unless the person asks for a look); "
                         "if vdc_submit is not among your tools, call vdc_queue with the upload_id instead. "
                         "Do not ask the person to upload the file again or to give a path.")
        if self.cfg("post_results", True):
            lines.append("The finished picture is posted into this chat by the bridge itself: tell the person it is queued "
                         "(relay the message the tool answers with) and stop; never try to download or relay the picture.")
        else:
            lines.append("When the job is done, call vdc_post_result with the job id to post the result into this chat.")
        note = "\n".join(lines)
        req.system_prompt = ((req.system_prompt or "").rstrip() + "\n\n" + note).strip()
        logger.info(f"[vdc] {len(files)} file(s) uploaded for the model")

    @filter.llm_tool(name="vdc_queue")
    async def vdc_queue(self, event: AstrMessageEvent, upload_id: str, preset: str = "default"):
        """Queues a file that the bridge uploaded to VRChat DLSS5 Cam (the upload_id it named) for DLSS 5 neural rendering, and answers with the job's place in the queue. The result is posted into this chat by itself.

        Args:
            upload_id(string): The upload id the bridge named for the picture
            preset(string): The look: the name of a preset saved in the program, or default
        """
        if not self.configured():
            return "The VRChat DLSS5 Cam plugin has no server address yet."
        name = (self.watch.get(upload_id) or {}).get("name", "picture")
        try:
            job = await self.queue(upload_id, name, event, preset or None)
        except Exception as e:
            return f"The server refused the job: {e}"
        return f"Queued as job {job.get('jobId')}. {job.get('message', '')} The result is posted into this chat when it is done."

    @filter.llm_tool(name="vdc_post_result")
    async def vdc_post_result(self, event: AstrMessageEvent, job_id: str):
        """Posts the finished result of a VRChat DLSS5 Cam job (its picture or video) into this chat. Call it when a job is done and the bridge is not posting results by itself.

        Args:
            job_id(string): The job id vdc_submit answered with
        """
        if not self.configured():
            return "The VRChat DLSS5 Cam plugin has no server address yet."
        try:
            job = await self.server().call("vdc_jobs", action="get", id=job_id)
        except Exception as e:
            return f"The job could not be read: {e}"
        state = job.get("state", "")
        if state not in FINAL_STATES:
            return f"The job is {state}: {job.get('message', '')}"
        self.done.add(job_id)
        n = await self.deliver(event.unified_msg_origin, job)
        return f"Posted {n} file(s) into the chat." if n else f"The job is {state}: {job.get('error', '') or 'nothing to post'}"

    @filter.command("vdc")
    async def cmd_vdc(self, event: AstrMessageEvent):
        """Processes the pictures of this message with VRChat DLSS5 Cam: /vdc [preset]. /vdc status shows the server, /vdc jobs the queue."""
        if not self.configured():
            yield event.plain_result("The VRChat DLSS5 Cam plugin has no server address yet (plugin settings).")
            return
        words = (event.message_str or "").strip().split()
        if words and words[0].lower().lstrip("/") == "vdc":
            words = words[1:]
        arg = " ".join(words).strip()
        if arg.lower() == "status":
            try:
                st = await self.server().call("vdc_get_status")
            except Exception as e:
                yield event.plain_result(f"The server did not answer: {e}")
                return
            yield event.plain_result(self.status_text(st))
            return
        if arg.lower() == "jobs":
            try:
                data = await self.server().call("vdc_jobs", action="list")
            except Exception as e:
                yield event.plain_result(f"The server did not answer: {e}")
                return
            jobs = data.get("jobs") or []
            lines = [f"{j.get('jobId')}  {j.get('state')}  {j.get('name', '')}  {j.get('message', '')}".rstrip() for j in jobs[-10:]]
            yield event.plain_result("\n".join(lines) if lines else "No jobs.")
            return
        files = await self.collect(event)
        if not files:
            yield event.plain_result("Send a picture or a video with the command, or reply to one with it.")
            return
        self._start_poller()
        lines = []
        for name, data in files:
            try:
                up = await self.server().upload(name, data)
                uid = str(up.get("uploadId", ""))
                self.watch[uid] = self.target(event, name)
                job = await self.queue(uid, name, event, arg or None)
                lines.append(f"{name}: {job.get('message', 'queued')}")
            except Exception as e:
                lines.append(f"{name}: {e}")
        yield event.plain_result("\n".join(lines))

    # --- results ---------------------------------------------------------------------------------------

    async def _poll(self) -> None:
        while True:
            try:
                await asyncio.sleep(max(1, int(self.cfg("poll_seconds", 3))))
                self._expire()
                if not (self.watch or self.jobs) or not self.configured():
                    continue
                data = await self.server().call("vdc_jobs", action="list")
                for j in data.get("jobs") or []:
                    jid = str(j.get("jobId", ""))
                    uid = str(j.get("uploadId", ""))
                    target = self.jobs.get(jid) or (self.watch.get(uid) if uid else None)
                    if not target or not jid or jid in self.done:
                        continue
                    if j.get("state") in FINAL_STATES:
                        self.done.add(jid)
                        self.jobs.pop(jid, None)
                        if uid:
                            self.watch.pop(uid, None)
                        if self.cfg("post_results", True):
                            await self.deliver(target["umo"], j)
                    elif jid not in self.jobs:
                        self.jobs[jid] = target
                        if uid:
                            self.watch.pop(uid, None)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                logger.warning(f"[vdc] watching the queue: {e}")

    def _expire(self) -> None:
        now = time.time()
        for uid in [u for u, t in self.watch.items() if now - t["at"] > UPLOAD_KEEP]:
            self.watch.pop(uid, None)
        for jid in [j for j, t in self.jobs.items() if now - t["at"] > JOB_KEEP]:
            self.jobs.pop(jid, None)
        if len(self.done) > 2000:
            self.done.clear()
        try:
            for f in os.listdir(self.dir):
                p = os.path.join(self.dir, f)
                if os.path.isfile(p) and now - os.path.getmtime(p) > FILE_KEEP:
                    os.remove(p)
        except Exception:
            pass

    async def deliver(self, umo: str, job: dict[str, Any]) -> int:
        """Posts a finished job into the chat: its files, or why it failed. Answers with the number of files."""
        jid = str(job.get("jobId", ""))
        name = job.get("name", "the file")
        state = job.get("state", "")
        chain = MessageChain()
        n = 0
        if state == "done":
            text = str(self.cfg("result_text", "") or "").strip()
            if text:
                chain.message(text)
            for o in job.get("outputs") or []:
                oname = str(o.get("name", ""))
                if not oname:
                    continue
                try:
                    data = await self.server().download(jid, oname)
                except Exception as e:
                    logger.warning(f"[vdc] {jid}/{oname} could not be downloaded: {e}")
                    continue
                path = os.path.join(self.dir, f"{jid}_{os.path.basename(oname)}")
                with open(path, "wb") as f:
                    f.write(data)
                ext = os.path.splitext(oname)[1].lower()
                if ext in PICTURE_EXT:
                    chain.chain.append(Comp.Image.fromFileSystem(path))
                elif ext in VIDEO_EXT:
                    chain.chain.append(Comp.Video.fromFileSystem(path))
                else:
                    chain.chain.append(Comp.File(name=oname, file_=path))
                n += 1
            if n == 0:
                chain.message(f"VRChat DLSS5 Cam: the job for {name} ended without a file.")
        elif state == "failed":
            chain.message(f"VRChat DLSS5 Cam could not process {name}: {job.get('error', '') or 'unknown error'}")
        else:
            chain.message(f"VRChat DLSS5 Cam: the job for {name} was cancelled.")
        try:
            await self.context.send_message(umo, chain)
        except Exception as e:
            logger.warning(f"[vdc] the result of {jid} could not be posted to {umo}: {e}")
        return n

    @staticmethod
    def status_text(st: dict[str, Any]) -> str:
        neural = st.get("neural") or {}
        adapter = st.get("adapter") or {}
        q = st.get("jobs") or {}
        lines = [f"VRChat DLSS5 Cam {st.get('version', '')} {st.get('edition', '')} on {adapter.get('name', '?')}",
                 "neural pass: " + ("ready" if neural.get("runtimeLoaded") else "not loaded") + (f" ({neural.get('error')})" if neural.get("error") else "")]
        if isinstance(q, dict):
            parts = [f"{k}: {v}" for k, v in q.items() if isinstance(v, (int, float, str)) and k in ("queued", "running", "kept", "waiting", "capacity", "perKey", "max")]
            if parts:
                lines.append("queue " + ", ".join(parts))
        return "\n".join(lines)


def _read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()
