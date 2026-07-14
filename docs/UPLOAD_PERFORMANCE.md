# BFpilot PC→PS5 upload performance (research + applied)

## Reality (PS5 architecture / scene measurements)

| Limit | Practical meaning |
| --- | --- |
| Gigabit LAN | ≈ **110–112 MiB/s** ceiling; cannot beat without faster net |
| `/data` PFS writes | Often **much** slower than raw SSD (encryption, VFS, scheduler, kstuff) |
| USB/ext | Sometimes faster than `/data` (different stack) |
| Firmware variance | zftpd-reported internal upload band ≈ 20–113 MB/s across FW |

Sources: workspace `notes/03-filesystem-io-performance.md`, ps5upload throughput analysis, zftpd STOR patterns, FreeBSD socket/`sendfile` docs.

## What was already good (pre this pass)

- Single-buffer STOR: `recv` → `write` → repeat (no double-buffer stall with small RCVBUF)
- No multi-GB `posix_fallocate` during streaming body (was causing 1–2 MB/s collapse)
- No `TCP_NODELAY` on bulk upload (tiny segments thrash PFS)
- Listen `SO_RCVBUF` 4 MiB (accepted sockets inherit)
- Free-space preflight via `statvfs`
- Staging temp + rename

## Applied optimizations (safe / compatibility-first)

| Change | Why safe | Expected impact |
| --- | --- | --- |
| **HTTP/1.1 keep-alive** (up to 64 req/conn) | Same semantics; close still works | **Multi-file**: cut TCP handshake + slow-start per file |
| **Upload buf 2 MiB** (was 1 MiB) | Still single-buffer STOR cadence | Fewer PFS `write` syscalls on write-bound FW |
| **Download stream buf 1 MiB** (was 64 KiB) | Read path only | Faster `/fs` downloads |
| **Listen SO_SNDBUF 2 MiB** + post-accept try | Kernel may cap | Better download window when inherited |
| **Listen backlog 32** (was 16) | Standard | Fewer accept drops under burst |
| **UI: less toast spam** (every 8 files) | Client-only | Less PS5 browser main-thread thrash |
| **Explicit `Content-Type: application/octet-stream`** | Spec-friendly | Cleaner request shape |

## Explicitly avoided (unstable or false speed)

| Idea | Why not |
| --- | --- |
| Parallel multi-writer to `/data` | PFS often serializes; can **slow** large transfers |
| Double-buffer upload | zftpd: stalls when RCVBUF small / window closes |
| Whole-file `posix_fallocate` during POST | Fills RCVBUF while browser streams → collapse |
| `TCP_NODELAY` on bulk | Tiny segments + PFS thrash |
| `F_NOCACHE` + later `sendfile` | Panic risk on some media |

## How to measure after deploy

1. Rebuild/inject GemBfpilot  
2. Upload one large file (≥500 MB) to `/data/...` and note JSON `averageMBps`, `recvMs`, `writeMs`  
3. Upload a folder of many small files — multi-file should improve most with keep-alive  
4. Compare Wi-Fi vs wired (wired expected much faster)

Server log line:

```text
upload end ... average_mbps=… recv_ms=… write_ms=… buf_size=2097152
```

If `write_ms` ≫ `recv_ms` → disk-bound (normal on slow FW `/data`).  
If `recv_ms` ≫ `write_ms` → network or window (check LAN, RCVBUF effective log at listen).

## Related files

- `src/transfer.c` — upload STOR loop  
- `src/websrv_lite.c` — listen buffers, keep-alive client loop  
- `src/fs.c` — download stream buffer  
- `assets/files.html` — browser upload queue  
- `scripts/goal_verify_io.py` — structural gates  
