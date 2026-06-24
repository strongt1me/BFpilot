# BFpilot Archive Extraction Report

Date: 2026-06-19

## Current Implementation

BFpilot keeps archive extraction in a separate payload:

- `bfpilot.elf` prepares archive jobs and exposes `/api/fs/archive/*`.
- `bfpilot-archive-worker.elf` performs extraction and writes progress.
- Jobs live under `/data/bfpilot/archive/`.
- Extraction is restricted to `/data`, `/mnt/usb0..7`, and `/mnt/ext0..7`.
- Output is staged in `*.bfpilot-extracting-*` and renamed to the final
  destination only after success.

This split keeps the file manager payload compatibility-focused and keeps the
archive engines out of the always-loaded main ELF.

## Supported Formats

- RAR through the vendored PS5 UnRAR path.
- 7z, passworded 7z, and `.7z.001` multipart sets through the vendored 7z path.
- ZIP stored/deflate, ZIP64 sizes/offsets, and ZipCrypto passwords through a
  small BFpilot ZIP reader using miniz inflate.

Known limits:

- ZIP AES encryption is detected and refused.
- Split ZIP is not implemented.
- Browser-side cancellation is not available after extraction starts.

## Live PS5 Results

Target: `192.168.1.204`, web port `5905`, payload port `9021`.

Build:

```sh
PS5_PAYLOAD_SDK=/home/blurf/PS5/ps5-payload-sdk make clean all inspect-imports
```

Result: passed. `inspect-imports` confirmed `bfpilot.elf` is still
file-manager-only, `bfpilot-launcher-installer.elf` contains the launcher
installer imports, and `bfpilot-archive-worker.elf` contains no AppInst imports.

Post-rebuild live reload:

```sh
python3 payload_sender.py 192.168.1.204 9021 bfpilot.elf
curl http://192.168.1.204:5905/api/status
curl http://192.168.1.204:5905/api/diag
curl http://192.168.1.204:5905/api/fs/archive/support
```

Result: passed. The rebuilt file manager ran as `pid=185`, reported
`mode=file-manager`, kept `/api/status` and `/api/diag` available, and reported
archive support for `rar`, `7z`, `7z.001`, and `zip`.

Post-rebuild file API smoke:

```sh
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 BF_ALLOW_PS5_WRITE=1 make ps5-smoke
```

Result: passed. The smoke test uploaded, downloaded, copied, renamed, moved,
and deleted only its own `/data/test/bfpilot-smoke-*` directory.

Post-rebuild archive smoke:

```sh
POST /api/fs/upload
path=/data/test
filename=bfpilot-final-archive-smoke-20260619T111147Z.zip

POST /api/fs/archive/prepare
src=/data/test/bfpilot-final-archive-smoke-20260619T111147Z.zip
dst=/data/test/bfpilot-final-archive-smoke-20260619T111147Z-out

python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
GET /fs/data/test/bfpilot-final-archive-smoke-20260619T111147Z-out/final-smoke/hello.txt
```

Result: `state=done`, `archiveType=zip`, `archiveExitCode=0`,
`totalFiles=1`, `bytesWritten=39`. Downloading the extracted member matched
the uploaded test content.

ZIP explicit directory entries:

```sh
POST /api/fs/upload
path=/data/test
filename=bfpilot-zip-direntry-20260619T203300Z.zip

POST /api/fs/archive/prepare
src=/data/test/bfpilot-zip-direntry-20260619T203300Z.zip
dst=/data/test/bfpilot-zip-direntry-20260619T203300Z-out

python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
GET /fs/data/test/bfpilot-zip-direntry-20260619T203300Z-out/dir/sub/hello.txt
```

Result: `state=done`, `archiveType=zip`, `archiveExitCode=0`,
`totalFiles=3`, `bytesWritten=51`. This validated ZIP entries that explicitly
store `dir/` and `dir/sub/` directory records.

Normal 7z:

```sh
POST /api/fs/archive/prepare
src=/data/test/bfpilot-20260619105720-plain.7z
dst=/data/test/bfpilot-20260619105720-plain-7z-out
python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
```

Result: `state=done`, `archiveType=7z`, `archiveExitCode=0`. Extracted
`plain/sub/hello.txt` matched the source text.

Passworded 7z:

```sh
POST /api/fs/archive/prepare
src=/data/test/bfpilot-20260619105720-encrypted.7z
dst=/data/test/bfpilot-20260619110357-encrypted-7z-pass-out
password=codextest
python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
```

Result: `state=done`, `archiveType=7z`, `archiveExitCode=0`,
`totalBytes=26`, `bytesWritten=26`. Extracted `encrypted/secret.txt` matched
the source text.

Password-required 7z:

```sh
POST /api/fs/archive/prepare
src=/data/test/bfpilot-20260619105720-encrypted.7z
dst=/data/test/bfpilot-20260619110342-encrypted-7z-nopass-out
password=
python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
```

Result: `state=error`, `archiveType=7z`, `error="7z password required"`,
`archiveExitCode=11`.

Multipart 7z:

```sh
POST /api/fs/archive/prepare
src=/data/test/bfpilot-20260619105720-split.7z.001
dst=/data/test/bfpilot-20260619105720-split-7z-out
python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
```

Result: `state=done`, `archiveType=7z`, `archiveExitCode=0`. The extracted
binary member SHA-256 matched the local source.

Provided RAR:

```sh
POST /api/fs/archive/prepare
src=/data/test/PPSA04220-app0.rar
dst=/data/test/bfpilot-20260619110408-PPSA04220-app0-rar-out
password=
python3 payload_sender.py 192.168.1.204 9021 bfpilot-archive-worker.elf
GET /api/fs/archive/status
```

Result: `state=error`, `archiveType=rar`,
`error="RAR password required or next multipart volume is missing"`,
`archiveExitCode=255`. It failed in about 32 ms, before data extraction, which
points to an encrypted-header/password prompt or an immediately missing volume.
The `/data/test` listing showed `PPSA04220-app0.rar` but no matching RAR volume
files with the same prefix.

ZIP and ZipCrypto ZIP were validated earlier in the same live session:

- `bfpilot-test.zip`: extracted one text file successfully.
- `bfpilot-enc.zip` with password `codextest`: extracted successfully after
  fixing the ZipCrypto key update path.

Standalone worker ZIP smoke:

```sh
python3 payload_sender.py 192.168.1.204 9021 bfpilot.elf
curl http://192.168.1.204:5907/api/fs/archive/support
POST /api/fs/upload
POST /api/fs/archive/prepare
GET /api/fs/archive/status
GET /fs/data/test/bfpilot-daemon-archive-20260619T214803Z/out/dir/hello.txt
```

Result on temporary port `5907`: passed with `bfpilot-archive-worker.elf`
injected. Final status was `state=done`,
`archiveType=zip`, `archiveExitCode=0`, `bytesWritten=11520`,
`totalFiles=2`, `elapsedMs=68`, `averageMBps=0.16`. The extracted file matched
the uploaded test content and `/api/status` still responded from server
`pid=274`.

Final local rebuild after the UI wording cleanup:

```sh
PS5_PAYLOAD_SDK=/home/blurf/PS5/ps5-payload-sdk make all inspect-imports
PS5_IP=192.168.1.204 BF_WEB_PORT=5907 make ps5-diag
```

Result: passed. The live archive worker endpoint reported
`requiresInjection=true`, `/api/fs/archive/status` still reported the completed
ZIP extraction above, and `make ps5-diag` saved
`diagnostics/ps5-diag-20260619T215312Z.json`.

Note: port `5905` could not be revalidated in the same session after an earlier
unsafe integrated-thread extraction test wedged the old listener. The final
release build is still compiled for port `5905`; the temporary `5907` run was
used only to prove the fixed daemon architecture without rebooting the console.

## Source Files

- `src/archive_worker.cpp`
- `src/transfer.c`
- `src/websrv_lite.c`
- `assets/files.html`
- `third_party/unrar-ps5/`
- `third_party/miniz/`
- `Makefile`
