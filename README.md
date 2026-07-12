# BFpilot

BFpilot is a PS5 payload that serves a browser-based file manager at `http://<PS5_IP>:5905/`.
It runs entirely from the PS5 web browser — no app install, no companion app, no extra hardware needed.

The project ships as two payloads:

- `bfpilot.elf` — the main file manager with integrated archive extraction.
- `bfpilot-launcher-installer.elf` — an optional home-screen tile installer (separate for firmware compatibility).

---

## Features

### File Management
- Dual-panel layout — left panel browses, right panel is the copy/move target
- Browse all major PS5 paths: Root, Data, Homebrew, User, System Data, mounted USB drives
- Upload files via drag-and-drop or a file picker (with real-time progress, speed, and ETA)
- Download files (zero-copy via FreeBSD `sendfile` for maximum speed)
- Copy and Move with background job tracking, progress bar, speed, and cancel support
- **Conflict Overwrite & Merge Prompts** — when a copy or move would overwrite an existing file or merge into an existing folder, a dialog asks: Overwrite / Merge / Skip — no silent data loss
- Rename files and folders
- Create folders
- Delete (recursive, with cancel support)
- Click-to-calculate folder sizes

### Selection
- **Checkbox multi-selection** — every row has a checkbox so you can select multiple files/folders without needing Ctrl+Click (PS5 controller friendly)
- **Select All** master checkbox in the column header
- Clicking the row body does single-select; clicking the checkbox toggles without deselecting others

### Search & Indexing
- **Index All** — multi-threaded work-stealing crawler that indexes all storage roots
- Instant query results — search resolves in `0ms` from the in-memory index
- Search results highlight matched terms in both the filename and the path
- Live index status bar (crawling / done / result count)
- Cancel indexing at any time

### Archive Extraction
- **ZIP** — Stored, Deflate, ZIP64, ZipCrypto password, multi-volume `.zip.001` / `.z01` splits
- **RAR** — RAR4 and RAR5, password-protected, multipart `.part1.rar` volume chains
- **7z** — LZMA/LZMA2, password-protected, split `.7z.001` sets
- Password prompt in the browser when the archive is encrypted
- Extract destination pre-fills from the active Target panel path
- Extraction progress bar with speed, bytes written, and ETA
- Partial output cleanup on error or cancel
- Integrated directly in `bfpilot.elf` — no second payload injection needed

### Image Viewer
- Built-in viewer for: `.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.webp`, `.ico`, `.svg`
- Click-and-drag **panning**
- Mouse-wheel **zooming** (0.1× – 10×)
- 90° **rotation** button
- **Reset** button to restore original zoom and position
- Double-click an image to open; `Escape` to close

### Text Editor
- Inline edit for: `.txt`, `.ini`, `.cfg`, `.log`, `.json`, `.xml`, `.js`, `.py`, `.sh`, `.md`, `.conf`, `.html`, `.css`, `.h`, `.c`, `.cpp`, and more
- Full-screen overlay editor, saves directly back to the PS5 filesystem

### Log Viewer
Log button opens a dropdown to pick which log to view:

| Log | Path on PS5 |
|-----|-------------|
| BFpilot server log | `/data/BFpilot/log.txt` |
| Archive extractor log | `/data/BFpilot/archive*/archive-worker.log` |
| Search crawler log | `/data/BFpilot/search-log.txt` |
| Boot log | `/data/BFpilot/boot.txt` |
| PS5 VSH system log | `/system_data/priv/log/vsh.log` |
| PS5 Error History | `/system_data/priv/error/history/` (decoded) |

The **PS5 Error History** option crawls Sony's crash record folder, decodes the JSON records, and displays error codes (e.g. `NW-102307-3`), Title IDs, firmware version, and timestamps in a human-readable list.

All logs auto-refresh every 2 seconds while the overlay is open.

### Exit Button
The top-right toolbar has an **Exit** button that shuts down BFpilot cleanly:
- Sends a loopback TCP wakeup to unblock the `accept()` loop instantly
- Stops the background archive daemon and search crawler threads gracefully
- Releases both port `5905` and loader port `9021` immediately
- **No console reboot needed** — you can re-inject the payload right away

### Navigation & Shortcuts
- Click-through path breadcrumbs to jump to any parent folder
- Persistent **custom shortcuts** — add, rename, and remove bookmarks saved to `/data/BFpilot/shortcuts.json`
- Storage summary (usable free / total / reserved) shown per drive
- USB drives appear in the sidebar only when physically connected

### Networking & Performance
- Zero-copy file downloads via FreeBSD `sendfile` syscall
- 2 MB socket send/receive buffers (`SO_SNDBUF` / `SO_RCVBUF`)
- `TCP_NODELAY` — eliminates Nagle algorithm latency
- Virtual DOM row recycling — handles 10,000+ file directories without the PS5 browser running out of memory (OOM)
- Case-insensitive `strcasestr` search — no duplicate lowercase index, 50% less RAM

### Permission Handling
BFpilot automatically sets permissions to `0777` on everything it creates (copies, uploads, extracted files, created folders). This bypasses PS5 filesystem permission issues transparently. There is no manual `chmod` UI — it is always applied automatically.

### Diagnostics
- `/api/status` — version, build tag
- `/api/diag` — full runtime diagnostics: socket bind/listen, UserService, NotificationService, launcher, last errno, first HTTP request
- SIGSEGV/SIGBUS crash handler — logs crash address to `/data/BFpilot/crash.txt`

---

## Download

Get the latest release from the [Releases page](https://github.com/ItsBlurf/BFpilot/releases).

- **`bfpilot.elf`** — inject this first, always. Contains the file manager and archive engine.
- **`bfpilot-launcher-installer.elf`** — inject only if you want the PS5 home-screen tile.

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
/data/BFpilot/boot.txt
/data/BFpilot/crash.txt
/data/BFpilot/archive-integrated/archive-worker.log
/data/BFpilot/archive-integrated/status.json
```

---

## Compatibility

`bfpilot.elf` is launcher-free by design. It avoids AppInstUtil, SystemService, UserService, and other privileged launcher imports that can fail before `main()` on some firmware and loader combinations. Archive extraction is integrated via libc-safe archive code only.

`bfpilot-launcher-installer.elf` is separate and uses the full launcher dependency set. If it fails on a firmware, the file manager remains fully usable.

See [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md) and [docs/FIRMWARE_TESTING.md](docs/FIRMWARE_TESTING.md) for details.

---

## Credits & Third-Party

- **[owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)** — The image viewer (zoom, pan, rotate) and checkbox multi-selection UI were inspired by owendswang's implementation. Their work was referenced when designing BFpilot's approach to those features.
- **[ps5-payload-dev](https://github.com/ps5-payload-dev)** — PS5 payload SDK, websrv, and related tooling.
- **[UnRAR source](https://www.rarlab.com/rar_add.htm)** — RAR extraction via the freeware UnRAR library (© Alexander Roshal).
- **[miniz](https://github.com/richgel999/miniz)** — ZIP decompression (public domain / Unlicense).
- **[LZMA SDK](https://www.7-zip.org/sdk.html)** — 7z decompression (public domain, Igor Pavlov).
