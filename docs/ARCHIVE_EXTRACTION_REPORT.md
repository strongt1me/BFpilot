# BFpilot Archive Extraction Report

Date: 2026-06-19
Updated: 2026-06-29

## Current Implementation

BFpilot now integrates archive extraction into the main file manager payload:

- `bfpilot.elf` prepares archive jobs, starts the archive daemon, performs
  extraction, and exposes `/api/fs/archive/*`.
- `bfpilot-archive-worker.elf` remains available as a fallback diagnostic
  daemon build of the same archive engine.
- Integrated jobs live under `/data/BFpilot/archive-integrated/`.
- The standalone diagnostic worker keeps the legacy `/data/BFpilot/archive/`
  paths.
- Extraction is restricted to `/data`, `/mnt/usb0..7`, and `/mnt/ext0..7`.
- Output is staged in `*.bfpilot-extracting-*` and renamed to the final
  destination only after success.

The launcher installer is still isolated in `bfpilot-launcher-installer.elf`.
The main ELF must remain free of AppInstUtil, SystemService, UserService, and
`kernel_sys` imports even though it now carries the archive engines.

## Supported Formats

- RAR through the vendored PS5 UnRAR path.
- 7z, passworded 7z, and `.7z.001` multipart sets through the vendored 7z path.
- ZIP stored/deflate, ZIP64 sizes/offsets, and ZipCrypto passwords through a
  small BFpilot ZIP reader using miniz inflate.

Known limits:

- ZIP AES encryption is detected and refused.
- Split ZIP is not implemented.
- Browser-side cancellation is not available after extraction starts.

## Performance Tuning

Host-verified changes on 2026-06-29:

- Removed the BFpilot-side clamp that forced every archive job to
  `threads=1`. Jobs now preserve `threads=0` as automatic engine tuning and
  clamp manual values to `1..8`.
- Removed the 7z adapter override that forced `mt=1`. The 7z handler now uses
  its automatic processor count unless a positive thread count is requested.
- Increased ZIP stored/deflate I/O buffers from 128 KiB stack buffers to
  1 MiB heap buffers.
- ZIP extraction now seeks once per entry and performs sequential reads inside
  the entry instead of seeking before every chunk.
- Archive status and logs now record `threads`, `threadMode`, elapsed time, and
  average MB/s for repeatable PS5 benchmarking.

The PS5 was unavailable for live testing during this tuning pass. Next live
validation should use the same archive set with `threads=0`, `1`, `2`, `4`, and
`8`, then keep the fastest stable setting for each archive type. Do not
benchmark user files; use BFpilot-owned paths under `/data/test`.

## Live PS5 Results

Target: `192.168.1.204`, web port `5905`, payload port `9021`.

Integrated one-ELF archive build on 2026-06-28:

```sh
make clean all inspect-imports
make deploy-bfpilot PS5_HOST=192.168.1.204 PS5_PORT=9021
curl http://192.168.1.204:5905/api/fs/archive/support
curl http://192.168.1.204:5905/api/fs/archive/status
```

Result: passed locally and live. `bfpilot.elf` reported
`worker=bfpilot-integrated-archive` and `requiresInjection=false`, while
`inspect-imports` still confirmed no launcher/AppInst imports in the main ELF.
A small ZIP smoke archive uploaded under `/data/test` extracted successfully
without injecting `bfpilot-archive-worker.elf`:

```text
src=/data/test/BFpilot-integrated-smoke-20260628T000205Z/smoke.zip
dst=/data/test/BFpilot-integrated-smoke-20260628T000205Z/out
```

Final status reported `state=done`, `archiveType=zip`,
`archiveExitCode=0`, `percent=100`, `totalFiles=3`, and `bytesWritten=79`.
Diagnostics and logs were saved in
`diagnostics/ps5-diag-20260628T000218Z.json`.

During repeated live reloads, an older pre-lifecycle archive daemon kept the
legacy BFpilot archive lock after its parent web process was replaced. The
integrated runtime now uses `/data/BFpilot/archive-integrated`, records its PID
in `daemon.lock`, and exits if its parent file manager process changes. This
prevents future stale integrated daemons and avoids racing old standalone
workers that are still watching `/data/BFpilot/archive`.

Final isolated integrated-runtime validation on 2026-06-28:

- Deployed `bfpilot.elf` through the SDK deployer on port `9021`.
- New file manager pid: `240`.
- Integrated archive daemon pid: `241`.
- `/api/fs/archive/support` reported
  `/data/BFpilot/archive-integrated/job.ini`,
  `/data/BFpilot/archive-integrated/status.json`, and
  `requiresInjection=false`.
- `/data/BFpilot/archive-integrated/daemon.lock` contained `241`.
- ZIP smoke path:
  `/data/test/BFpilot-integrated-v2-20260628T102757Z`.
- Final status: `state=done`, `archiveType=zip`, `archiveExitCode=0`,
  `percent=100`, `totalFiles=3`, `bytesWritten=83`,
  `requiresInjection=false`.
- Extracted `hello.txt` and `dir/nested.txt` matched the uploaded ZIP content.
- Read-only diagnostics and logs were saved in
  `diagnostics/ps5-diag-20260628T102910Z.json`.

Full integrated archive validation on 2026-06-28:

- Live target: `192.168.1.204`, web port `5905`, payload port `9021`.
- Build: `make all inspect-imports` after the integrated archive changes.
- Deployment: `make deploy-bfpilot PS5_HOST=192.168.1.204 PS5_PORT=9021`.
- New file manager pid: `253`.
- Integrated archive daemon pid: `254`.
- `/api/fs/archive/support` reported `requiresInjection=false`.
- Small archive matrix: 24/24 checks passed under
  `/data/test/BFpilot-archive-full-20260628T103318Z`, then the remote test
  directory was deleted and verified gone.
- The matrix covered normal ZIP, ZipCrypto ZIP with correct password,
  ZipCrypto ZIP with wrong and missing passwords, normal 7z, encrypted-header
  7z with correct, wrong, and missing passwords, split `.7z.001`, unsafe ZIP
  member refusal, unsupported random input, unsupported AES ZIP, destination
  existence refusal, and cleanup.
- Correctly handled failures included `bad zip password`,
  `zip entry needs a password`, `7z password required`,
  `unsafe zip member path: ../evil.txt`, `unsupported archive type`, and
  `zip AES encryption is not supported yet`.
- Encrypted-header 7z with the wrong password now reports
  `bad 7z password or encrypted-header open error` with
  `archiveExitCode=11`. This replaced the older misleading
  `missing 7z volume or open error` result for that case.
- Large ZIP64 test: uploaded a 5 GiB stored ZIP to
  `/data/test/BFpilot-large-20260628T103420Z/big-zero-5g.zip`.
  BFpilot reported upload size `5368709416`, elapsed `872127 ms`, and
  `5.87 MB/s`.
- Large extraction result: `state=done`, `archiveType=zip`,
  `archiveExitCode=0`, `percent=100`, `bytesWritten=5368709120`,
  `totalBytes=5368709120`, `filesDone=1`, elapsed `117454 ms`, and
  `43.59 MB/s`.
- The extracted 5 GiB file size was verified through `/api/fs/list`.
- The large remote test directory was deleted after testing and verified with
  a `404` listing response.
- RAR matrix after installing RARLAB `rar` 7.22 locally:
  `/data/test/BFpilot-rar-matrix-20260628T111331Z` covered plain RAR,
  passworded RAR with correct password, passworded RAR with wrong password,
  and passworded RAR with missing password. Final states were `done`, `done`,
  `error`, and `error`.
- RAR password diagnostics from the live run: wrong password returned
  `bad RAR password or checksum error` with `archiveExitCode=11`; missing
  password returned `RAR password required or archive checksum error` with
  `archiveExitCode=3`.
- Multipart RAR after installing RARLAB `rar` 7.22 locally:
  `/data/test/BFpilot-rar-split-20260628T111408Z` uploaded
  `split.part1.rar` through `split.part4.rar`; extraction from
  `split.part1.rar` completed with `state=done`, `archiveType=rar`,
  `archiveExitCode=0`, and `bytesWritten=196939`.
- Both generated RAR remote test directories were deleted after testing and
  verified with `404` listing responses.
- Read-only diagnostics and logs were saved in
  `diagnostics/ps5-diag-20260628T110002Z.json`.
- After a final `make clean all inspect-imports`, the rebuilt
  `bfpilot.elf` was redeployed and ran as file manager pid `255` with archive
  daemon pid `256`. Final read-only diagnostics were saved in
  `diagnostics/ps5-diag-20260628T110505Z.json`.

Two issues found during the full validation were fixed:

- Integrated daemon handoff race: a newly deployed file manager could start a
  daemon while the old daemon still held `archive-integrated/daemon.lock`,
  causing the new daemon to exit and the old daemon to exit after parent
  change. The daemon now retries the lock briefly before failing, which lets
  live reloads hand off cleanly.
- Encrypted-header 7z wrong-password reporting: the 7z opener previously
  classified wrong passwords as a generic open/missing-volume error. The
  result is now password-specific when a password was supplied.

Current generated RAR coverage now includes locally generated RAR 7.22
archives, passworded archives, wrong/missing password handling, and multipart
RAR5 volumes. Older RAR2/RAR3 compatibility should still be checked with
known-good sample files if those formats are important for a release.

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
password=testpass
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
- `bfpilot-enc.zip` with password `testpass`: extracted successfully after
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
