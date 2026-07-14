# Archive extraction

BFpilot extracts archives **inside** `bfpilot.elf` (integrated daemon). Users do not inject a second archive payload for normal use.

`bfpilot-archive-worker.elf` is the same engine built as a standalone diagnostic binary only.

## Paths

| Integrated (normal) | Standalone diagnostic worker |
|---------------------|------------------------------|
| `/data/BFpilot/archive-integrated/` | `/data/BFpilot/archive/` |

Job/status/log files live under those directories (`job.ini`, `status.json`, `archive-worker.log`, `daemon.lock`).

## Supported formats

Advertised by `/api/fs/archive/support`:

| Format | Notes |
|--------|--------|
| **RAR** | RAR4/RAR5, passwords, multipart `.partN.rar` / `.rNN`. Multi-thread RAR extract is restricted for stability (auto typically 1 thread). |
| **7z** | LZMA/LZMA2, passwords, split `.7z.001` sets. Threading up to a small auto max / higher manual max. |
| **ZIP** | Stored, Deflate, ZIP64 sizes/offsets, **ZipCrypto** passwords only. **ZIP AES is detected and refused.** Multipart/split ZIP is **not** advertised as supported. |

## Limits & safety

- Extract destinations must be under **`/data`**, **`/mnt/usb0–7`**, or **`/mnt/ext0–7`**.
- Output is staged under a temporary name and renamed after success when possible.
- Free-space checks use a margin before large extracts.
- Partial cleanup on failure is available when enabled (`cleanupPartial`).
- Browser cancel after extraction has started is **not** implemented (UI reports this).
- Password is supplied through the prepare API / browser prompt.

## UI flow

1. Select a supported archive in the file list.
2. Set the **Target** path (or accept the pre-filled target).
3. **Extract** → optional password → job progress from `/api/fs/archive/status`.

## Engines (vendored)

- UnRAR (RAR) — `third_party/unrar-ps5/`
- LZMA SDK / 7z — under `third_party/unrar-ps5/lzma2601/`
- miniz — ZIP inflate — `third_party/miniz/`
