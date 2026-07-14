# BFpilot

BFpilot is a PS5 homebrew payload that serves a browser-based file manager at:

```text
http://<PS5_IP>:5905/
```

Use it from the PS5 web browser or any device on the same LAN. No companion PC app is required after the payload is injected.

**Current version:** `v0.4.0` (`bfpilot-v0.4.0`)

**Validated on:** digital PS5 firmware **11.60** (elfldr-style inject on port **9021**).

> Network services are **trusted LAN only**. Do not expose port `5905` to the public internet.

---

## What you get

| Payload | Role |
|--------|------|
| **`bfpilot.elf`** | Main file manager + integrated archive extraction. **Inject this first.** |
| **`bfpilot-launcher-installer.elf`** | Optional home-screen tile installer (title id `BFPL00001`). Separate binary for firmware compatibility. |
| `bfpilot-archive-worker.elf` | Optional **build-only diagnostic** of the same archive engine. Normal users do **not** need it; extraction is already inside `bfpilot.elf`. |

Prebuilt release assets ship the two ELFs above. Building from source also produces the archive-worker binary.

---

## Features

### File management

- Dual-panel layout: left browses; right **Target** panel is the copy / move / extract destination
- Sidebar places: Root (`/`), Data (`/data`), Homebrew (`/data/homebrew`), User (`/user`), Mounts (`/mnt`), connected USB (`/mnt/usb0–7`) and external (`/mnt/ext0–7`) volumes when present, plus custom shortcuts
- Other paths (for example under `/system_data`) via Root navigation or a custom shortcut
- **Upload Files** and **Upload Folder** via the system file pickers (progress, speed, ETA). Folder upload needs a browser that supports `webkitdirectory` (PS5 browser may not)
- **Download** selected files over HTTP (buffered `read` → socket `write`, **1 MiB** stream buffer — not `sendfile`)
- **Copy** / **Move** with background jobs, progress bar, speed, cancel
- Free-space preflight before copy/move when sizes are known
- **Conflict prompts** (Overwrite / Merge / Skip) when destinations collide
- Rename, create folder, recursive delete (with cancel)
- Click a folder’s size cell to calculate directory size on demand

### Selection

- Checkbox multi-select (controller-friendly)
- Select-all master checkbox in the list header
- Row body click = single select; checkbox toggles without clearing others

### Search & Index All

- **Index All** builds an in-memory path index (Everything-style: crawl once, query RAM)
- Crawl runs on a **background thread**, sequentially over roots (multi-worker crawl was dropped for stability under common jailbreak loaders)
- Default `all` mode collects **priority + detected** roots, then crawls each with **same-device (XDEV)** fencing:
  - **first:** `/data` (and useful subtrees such as `/data/homebrew`), `/user` (app/meta/download/savedata/…), `/mnt/usb0–7`, `/mnt/ext0–7`
  - **then:** `/system`, `/system_data`, `/system_ex`, `/preinst*`, `/hostapp`, other distinct top-level mounts, and `/`
  - **never as full roots:** `/mnt` hub, `/system_tmp`, `/update`, `/mnt/sandbox`, `/mnt/shadowmnt`, `/dev`, `/proc`, `/sys`, `/net`, `/run`
  - mountpoint directory names may appear in the index; crawlers **do not cross `st_dev`**
  - **soft-fail:** one bad root does not discard the rest; status reports `rootsOk` / `rootsFail`
  - hard cap ~**2,000,000** entries (`truncated` if hit)
- Queries: case-insensitive multi-term match (`strcasestr`) over the index (default **200** results, max **1000** per request)
- Match highlighting in the UI; sizes/mtimes from indexed `lstat` data
- Live status: entry counts, timing, root success/failure lists; cancel keeps a partial index when possible
- Optional on-disk cache: `/data/BFpilot/search.idx`

### Archive extraction (integrated in `bfpilot.elf`)

| Format | Support |
|--------|---------|
| **ZIP** | Stored, Deflate, ZIP64, ZipCrypto passwords. **ZIP AES is refused.** Multipart/split ZIP is **not** advertised as supported. |
| **RAR** | RAR4 / RAR5, password, multipart `.partN.rar` / `.rNN` chains. Multi-thread RAR extract is limited for stability. |
| **7z** | LZMA / LZMA2, password, split `.7z.001` sets |

- Password prompt in the browser when needed
- Extract destination pre-fills from the **Target** panel path
- Allowed extract roots: **`/data`**, **`/mnt/usb0–7`**, **`/mnt/ext0–7`**
- Progress with speed, bytes written, and ETA
- Partial output cleanup on error (browser cancel after extract starts is **not** available yet)
- No second payload injection for normal extract

### Image viewer

- `.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.webp`, `.ico`, `.svg`
- Click-drag pan, mouse-wheel zoom (~0.1×–10×), 90° rotate, reset
- Double-click an image row to open; `Escape` to close

### Text editor

- Inline edit for common text-like extensions:  
  `.txt`, `.ini`, `.cfg`, `.conf`, `.log`, `.json`, `.xml`, `.js`, `.py`, `.sh`, `.bat`, `.html`, `.css`, `.h`, `.c`, `.cpp`, `.yaml`, `.yml`, `.sql`
- Files must be under **5 MiB**
- Full-screen overlay; save writes back to the PS5 filesystem

### Log viewer

Toolbar **Logs** button → dropdown:

| Log | Path on PS5 |
|-----|-------------|
| BFpilot server log | `/data/BFpilot/log.txt` |
| Archive extractor log | `/data/BFpilot/archive-integrated/archive-worker.log` |
| Search crawler log | `/data/BFpilot/search_crawl.log` |
| Boot log | `/data/BFpilot/boot.log` |
| PS5 VSH log | `/system_data/priv/vshlog/vshlog.0.txt` |
| PS5 Error History | `/system_data/priv/error/history/` (decoded in the UI when readable) |

Auto-refreshes about every 2 seconds while open.

### Exit

Top-right **Exit** calls `/api/control/shutdown` (token-protected):

- Stops background archive / search work where possible
- Releases port **5905** so you can re-inject without rebooting the console
- Does **not** stop the ELF loader (typically still on **9021**)

### Navigation & shortcuts

- Path bar + breadcrumbs (Up / Go / Refresh)
- Persistent custom shortcuts in `/data/BFpilot/shortcuts.txt` (add / remove / rename)
- Storage summary (usable free / total / reserved) when volume stats are available
- USB/ext entries appear only when those mount paths exist

### Networking & performance (v0.4.0)

- HTTP/1.1 **keep-alive** (up to **64** requests per connection) — helps multi-file upload
- Upload STOR: **2 MiB** single-buffer `recv` → `write` (no multi-GB `posix_fallocate` while the body streams; no bulk `TCP_NODELAY`)
- Download stream buffer: **1 MiB**
- Listen socket: **SO_RCVBUF 4 MiB**, **SO_SNDBUF 2 MiB**, backlog **32** (kernel may cap; values re-applied post-accept)
- Copy/move uses large sequential buffers
- Virtual row recycling in the file list for huge directories
- Search index does not store a second lowercase copy of every path

See [docs/UPLOAD_PERFORMANCE.md](docs/UPLOAD_PERFORMANCE.md).

### Permissions

Created files/dirs (upload, copy, extract, mkdir) get mode **`0777`** via `umask(0)` plus `chmod` / `fchmod`. There is no manual chmod UI.

### Diagnostics (HTTP)

| Endpoint | Purpose |
|----------|---------|
| `GET /api/status` (or `/api/version`) | Version / build tag |
| `GET /api/diag` | Runtime diagnostics (bind/listen, notify probe, errno, first request, …) |
| `GET /api/fs/places` | Sidebar places + storage stats |
| `GET /api/fs/*` | File manager API (list, upload, copy, search, archive, …) |

Fatal signals log to `/data/BFpilot/crash.log` when possible.

---

## Download & install

Latest binaries: **[Releases](https://github.com/ItsBlurf/BFpilot/releases)**

1. Jailbreak and keep an ELF loader listening (usually **`9021`** / elfldr).
2. Inject **`bfpilot.elf`**.
3. Open `http://<PS5_IP>:5905/`.
4. Optionally inject **`bfpilot-launcher-installer.elf`** for a home-screen tile that opens `http://127.0.0.1:5905/`.

```sh
python3 payload_sender.py <PS5_IP> 9021 bfpilot.elf
python3 payload_sender.py <PS5_IP> 9021 bfpilot-launcher-installer.elf
```

You can also send the ELFs with community loaders such as [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager) or [Blurfer](https://github.com/ItsBlurf/Blurfer).

Health checks:

```text
http://<PS5_IP>:5905/api/status
http://<PS5_IP>:5905/api/diag
http://<PS5_IP>:5905/api/fs/places
```

---

## Build

Requires [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and a host that can run the Prospero toolchain wrappers (Linux, macOS, or Windows with a Unix-like environment / Git Bash).

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
make inspect-imports
```

On Windows you can use `build.cmd` if your environment is already set up.

Expected outputs:

```text
bfpilot.elf
bfpilot-launcher-installer.elf
bfpilot-archive-worker.elf
```

Optional slim build without integrated archives:

```sh
make bfpilot-lite
```

`make inspect-imports` checks that `bfpilot.elf` has no AppInstUtil / launcher installer imports, and that the launcher installer contains the expected ones.

Default HTTP port is **5905** (`WEB_PORT` override is for local experiments only).

---

## Developer diagnostics & tests

These targets need a reachable PS5 and `PS5_IP` (or hostname). They are for developers; end users only need the ELFs.

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-diag
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-storage-audit
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 BF_ALLOW_PS5_WRITE=1 make ps5-smoke
```

Archive performance harness (writes under BFpilot-owned test paths when enabled):

```sh
BF_ALLOW_PS5_WRITE=1 \
BF_ARCHIVE_LOCAL=/path/to/test.rar \
BF_ARCHIVE_PASSWORD='optional-password' \
BF_ARCHIVE_THREADS=0,1,2 \
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 \
make ps5-archive-perf
```

Structural gates (no hardware): `python3 scripts/goal_verify_io.py`

Runtime paths on the PS5:

```text
/data/BFpilot/log.txt
/data/BFpilot/boot.log
/data/BFpilot/crash.log
/data/BFpilot/search_crawl.log
/data/BFpilot/search.idx
/data/BFpilot/shortcuts.txt
/data/BFpilot/archive-integrated/archive-worker.log
/data/BFpilot/archive-integrated/status.json
/data/BFpilot/launcher-installer.log   # after running the tile installer
```

---

## Compatibility

`bfpilot.elf` is **launcher-free**: no AppInstUtil / UserService / SystemService / `kernel_sys` installer imports that can fail before `main()` on some firmware and loader combinations. Archive extraction uses libc-safe integrated engines only.

`bfpilot-launcher-installer.elf` is separate and uses the full installer dependency set. If the tile install fails on a firmware, the file manager still works.

Details: [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md).

---

## Documentation

| Doc | Contents |
|-----|----------|
| [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md) | Payload split, imports, tile installer |
| [docs/UPLOAD_PERFORMANCE.md](docs/UPLOAD_PERFORMANCE.md) | Upload/download tuning and what we deliberately avoid |
| [docs/ARCHIVE_EXTRACTION.md](docs/ARCHIVE_EXTRACTION.md) | Formats, limits, allowed extract roots |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

---

## Credits & third-party

- **[owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)** — image viewer and checkbox multi-select UI inspiration
- **[ps5-payload-dev](https://github.com/ps5-payload-dev)** — PS5 payload SDK, websrv, and related tooling
- **[UnRAR](https://www.rarlab.com/rar_add.htm)** — RAR extraction (freeware UnRAR library, © Alexander Roshal)
- **[miniz](https://github.com/richgel999/miniz)** — ZIP inflate (public domain / Unlicense)
- **[LZMA SDK](https://www.7-zip.org/sdk.html)** — 7z decompression (public domain, Igor Pavlov)

---

## License note

Third-party trees under `third_party/` keep their own licenses (see UnRAR `license.txt` and upstream notices). Project-specific source is provided for homebrew use with the PS5 payload ecosystem.
