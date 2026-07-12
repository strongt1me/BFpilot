# BFpilot

BFpilot is a PS5 payload that serves a browser-based file manager at `http://<PS5_IP>:5905/`.
It runs from the PS5 web browser (or any device on the same LAN) — no companion PC app is required after injection.

The project builds these payloads:

- `bfpilot.elf` — main file manager with integrated archive extraction (inject this first).
- `bfpilot-launcher-installer.elf` — optional home-screen tile installer (separate for firmware compatibility).
- `bfpilot-archive-worker.elf` — optional standalone archive-worker build kept for diagnostics; normal use does not require injecting it.

---

## Features

### File Management
- Dual-panel layout — left panel browses; right **Target** panel is the copy/move/extract destination
- Sidebar places: Root (`/`), Data (`/data`), Homebrew (`/data/homebrew`), User (`/user`), Mounts (`/mnt`), connected USB/ext volumes, and custom shortcuts
- Other paths (for example under `/system_data`) are reachable by navigating from Root or via a custom shortcut; they are not all listed as dedicated sidebar places
- Upload files via drag-and-drop or a file picker (progress, speed, and ETA in the UI)
- Download files over HTTP (streamed with buffered `read`/`write` — not zero-copy `sendfile` in the current code)
- Copy and Move with background job tracking, progress bar, speed, and cancel support
- **Conflict Overwrite & Merge Prompts** — when a copy or move would overwrite an existing file or merge into an existing folder, the UI asks Overwrite / Merge / Skip
- Rename files and folders
- Create folders
- Delete (recursive, with cancel support)
- Click-to-calculate folder sizes

### Selection
- **Checkbox multi-selection** — every row has a checkbox (PS5 controller friendly)
- **Select All** master checkbox in the column header
- Clicking the row body does single-select; clicking the checkbox toggles without deselecting others

### Search & Indexing
- **Index All** rebuilds an in-memory path index using a multi-threaded work-stealing crawler
- It does **not** guarantee a full crawl of every path on the console. The rebuild (default root label `all`) collects **detected** search roots, then crawls each one:
  - always starts from `/`
  - adds known separate mounts when they exist as directories, including candidates such as `/system`, `/system_data`, `/system_ex`, `/preinst`, `/preinst2`, `/hostapp`, `/data`, `/user`
  - also picks up other top-level directories under `/` that are on a **different** device id than `/`
  - adds present `/mnt/usb0`–`/mnt/usb7` and `/mnt/ext0`–`/mnt/ext7` mounts
  - **skips** pseudo/volatile trees such as `/dev`, `/proc`, `/sys`, `/net`, `/run`, and `/mnt/sandbox`
  - **skips** missing paths and paths that fail `lstat`
  - hard cap of about **2,000,000** indexed entries (rebuild can report `truncated`)
- Search queries scan the in-memory index with case-insensitive matching (`strcasestr`); latency is usually low once the index exists, but it is not a fixed `0ms` guarantee
- Results can highlight matched terms in the filename and path
- Live index status (running / done / counts); indexing can be cancelled
- Search is only useful after a successful rebuild; an empty index returns an error until Index All has finished

### Archive Extraction
- **ZIP** — Stored, Deflate, ZIP64, ZipCrypto password, multi-volume `.zip.001` / `.z01` splits
- **RAR** — RAR4 and RAR5, password-protected, multipart `.partN.rar` volume chains
- **7z** — LZMA/LZMA2, password-protected, split `.7z.001` sets
- Password prompt in the browser when the archive is encrypted
- Extract destination pre-fills from the active Target panel path
- Extraction progress bar with speed, bytes written, and ETA
- Partial output cleanup on error or cancel
- Integrated in `bfpilot.elf` — no second payload injection needed for normal extract

### Image Viewer
- Built-in viewer for: `.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.webp`, `.ico`, `.svg`
- Click-and-drag **panning**
- Mouse-wheel **zooming** (about 0.1× – 10×)
- 90° **rotation** button
- **Reset** button to restore original zoom and position
- Double-click an image to open; `Escape` to close

### Text Editor
- Inline edit for common text-like extensions (for example `.txt`, `.ini`, `.cfg`, `.log`, `.json`, `.xml`, `.js`, `.py`, `.sh`, `.md`, `.conf`, `.html`, `.css`, `.h`, `.c`, `.cpp`)
- Full-screen overlay editor; saves back to the PS5 filesystem (size limits apply in the UI)

### Log Viewer
Log button opens a dropdown to pick which log to view:

| Log | Path on PS5 |
|-----|-------------|
| BFpilot server log | `/data/BFpilot/log.txt` |
| Archive extractor log | `/data/BFpilot/archive-integrated/archive-worker.log` |
| Search crawler log | `/data/BFpilot/search_crawl.log` |
| Boot log | `/data/BFpilot/boot.log` |
| PS5 VSH log | `/system_data/priv/vshlog/vshlog.0.txt` |
| PS5 Error History | `/system_data/priv/error/history/` (decoded in the UI) |

The **PS5 Error History** option lists crash records under Sony’s history folder when readable, and formats fields such as error codes, Title IDs, and timestamps when present.

Logs auto-refresh every 2 seconds while the overlay is open.

### Exit Button
The top-right toolbar has an **Exit** button that shuts down BFpilot cleanly:
- Calls `/api/control/shutdown` (token-protected)
- Unblocks the `accept()` loop and stops background archive/search work where possible
- Releases BFpilot’s web port **`5905`** so you can re-inject without a console reboot
- Does **not** stop the separate ELF loader service (typically still listening on **`9021`**)

### Navigation & Shortcuts
- Click-through path breadcrumbs to jump to any parent folder
- Persistent **custom shortcuts** — add, rename, and remove bookmarks saved to `/data/BFpilot/shortcuts.txt`
- Storage summary (usable free / total / reserved) shown for places that report volume stats
- USB/ext drives appear in the sidebar only when the mount path exists

### Networking & Performance
- Buffered HTTP downloads (read from file, write to socket)
- 2 MB socket send/receive buffer requests (`SO_SNDBUF` / `SO_RCVBUF`) on accepted connections
- `TCP_NODELAY` on accepted connections
- Virtual row recycling in the file list UI for large directories
- Case-insensitive search without storing a second lowercase copy of every path string

### Permission Handling
BFpilot sets mode `0777` on files and directories it creates (copies, uploads, extracted files, created folders) via `umask(0)` plus `chmod`/`fchmod`. There is no manual chmod UI.

### Diagnostics
- `/api/status` — version / build information
- `/api/diag` — runtime diagnostics (bind/listen state, notification probe results, last errno, first HTTP request, and related fields)
- Fatal signal handler logs to `/data/BFpilot/crash.log` on crashes such as SIGSEGV/SIGBUS

---

## Download

Get the latest release from the [Releases page](https://github.com/ItsBlurf/BFpilot/releases).

- **`bfpilot.elf`** — inject this first. File manager and integrated archive engine.
- **`bfpilot-launcher-installer.elf`** — only if you want the PS5 home-screen tile.

---

## Usage

Inject the payload to your loader (usually port `9021`):

```sh
python3 payload_sender.py <PS5_IP> 9021 bfpilot.elf
```

Open the web UI:

```
http://<PS5_IP>:5905/
```

Health check endpoints:

```
http://<PS5_IP>:5905/api/status
http://<PS5_IP>:5905/api/diag
http://<PS5_IP>:5905/api/fs/places
```

Install or refresh the launcher tile:

```sh
python3 payload_sender.py <PS5_IP> 9021 bfpilot-launcher-installer.elf
```

The tile opens `http://127.0.0.1:5905/`.

---

## Build

Set `PS5_PAYLOAD_SDK` to your PS5 payload SDK path:

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
make inspect-imports
```

Expected outputs:

```
bfpilot.elf
bfpilot-launcher-installer.elf
bfpilot-archive-worker.elf
```

`make inspect-imports` verifies that `bfpilot.elf` contains no launcher/AppInstUtil imports and that the installer contains the required ones.

---

## Diagnostics & Testing

Read-only diagnostics:

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-diag
```

Storage audit (free-space mismatch investigation):

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-storage-audit
```

Smoke test (uses only files under `/data/test`):

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 BF_ALLOW_PS5_WRITE=1 make ps5-smoke
```

Archive performance harness:

```sh
BF_ALLOW_PS5_WRITE=1 \
BF_ARCHIVE_LOCAL=/path/to/test.rar \
BF_ARCHIVE_PASSWORD='optional-password' \
BF_ARCHIVE_THREADS=0,1,2 \
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 \
make ps5-archive-perf
```

Runtime logs on the PS5:

```
/data/BFpilot/log.txt
/data/BFpilot/boot.log
/data/BFpilot/crash.log
/data/BFpilot/search_crawl.log
/data/BFpilot/archive-integrated/archive-worker.log
/data/BFpilot/archive-integrated/status.json
```

---

## Compatibility

`bfpilot.elf` is launcher-free by design. It avoids AppInstUtil and related installer imports that can fail before `main()` on some firmware and loader combinations. Archive extraction is integrated via libc-safe archive code only.

`bfpilot-launcher-installer.elf` is separate and uses the full installer dependency set (including UserService / AppInstUtil). If it fails on a firmware, the file manager remains usable.

See [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md) and [docs/FIRMWARE_TESTING.md](docs/FIRMWARE_TESTING.md) for details.

---

## Credits & Third-Party

- **[owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)** — Image viewer and checkbox multi-selection UI were inspired by owendswang’s work.
- **[ps5-payload-dev](https://github.com/ps5-payload-dev)** — PS5 payload SDK, websrv, and related tooling.
- **[UnRAR source](https://www.rarlab.com/rar_add.htm)** — RAR extraction via the freeware UnRAR library (© Alexander Roshal).
- **[miniz](https://github.com/richgel999/miniz)** — ZIP decompression (public domain / Unlicense).
- **[LZMA SDK](https://www.7-zip.org/sdk.html)** — 7z decompression (public domain, Igor Pavlov).
