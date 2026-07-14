# Changelog

## v0.4.0 Stable (2026-07-14)

Stable release after v0.3.x test builds. Focus: **PC→PS5 upload speed**, Index All stability, live validation on digital **FW 11.60**.

### Upload / network

* HTTP/1.1 **keep-alive** (up to 64 requests per connection) for multi-file uploads.
* **2 MiB** single-buffer STOR (`recv` → `write`; no double-buffer; no multi-GB `posix_fallocate` while the body streams).
* Listen **SO_RCVBUF 4 MiB**, **SO_SNDBUF 2 MiB**, backlog 32; no bulk `TCP_NODELAY` on upload.
* Download stream buffer **1 MiB** (buffered read/write — not `sendfile`).
* UI: less toast spam on multi-file upload; `Content-Type: application/octet-stream`.
* See `docs/UPLOAD_PERFORMANCE.md`.

### Index All / FS (carried from v0.3.9)

* Multi-root crawl with **same-device (XDEV)** fencing and soft-fail roots.
* Shared visited `(st_dev, st_ino)` with `st_dev == 0` fix; always `lstat` for sizes/mtimes.
* Crawl runs on a **background thread**, sequential per root (stable under common jailbreak loaders).
* Copy/move free-space preflight, large sequential buffers, fatal FS abort on hard I/O errors.

### Testing

* `scripts/goal_verify_io.py` structural gates.
* `scripts/ps5_smoke.py` and `scripts/ps5_full_feature_test.py` (44/44 live on 11.60 in release validation).

---

## v0.3.10 Upload speed (safe)

* HTTP keep-alive (64 req/conn), upload buffer 2 MiB, download 1 MiB, listen SO_SNDBUF 2 MiB + backlog 32.
* UI multi-file toast throttling; explicit octet-stream content type.

---

## v0.3.9 Test Build

* Web upload STOR path (single-buffer recv→write); no multi-GB fallocate during body; no bulk TCP_NODELAY; listen SO_RCVBUF 4 MiB. Server reports `averageMBps` / `recvMs` / `writeMs`.
* Index All: multi-root crawl, XDEV, soft-fail roots, expanded roots, rate logs.
* Copy/move: free-space preflight; overwrite unlink errno fix; large sequential buffers.
* Build: `ENABLE_ARCHIVE=0` / `bfpilot-lite` for slim ELF without unrar/7z/miniz.
* Verify: `scripts/goal_verify_io.py`.

---

## v0.3.8 Test Build Clean

* Conflict overwrite/merge prompts for copy/move (`409` + UI Overwrite / Merge / Skip).
* Checkbox multi-selection and Select All.
* Interactive image viewer (pan / zoom / rotate).
* Multi-log viewer including PS5 error history decode.
* Global `umask(0)` + `0777` on created files/dirs.
* ZIP ZipCrypto / ZIP64 improvements; extract dialog defaults to Target path.
* Search unified to a single **Index All** mode.

---

## v0.3.5 and earlier (summary)

* Graceful Exit / re-injection (loopback wakeup, clean port release on 5905).
* Integrated archive extraction in the main ELF (RAR / 7z / ZIP).
* Dark web UI with virtual scrolling for large directories.
* Search index without a second lowercase path string (`strcasestr`).
* Launcher tile isolated in `bfpilot-launcher-installer.elf`.

Older experimental networking notes (historical `sendfile` / bulk `TCP_NODELAY` experiments) are **not** the current design; see v0.4.0 and `docs/UPLOAD_PERFORMANCE.md`.
