# BFpilot Root-Cause Audit

Audit date: 2026-06-12

## Safety Finding

The previous `/api/diag` and `/api/fs/usb` implementations were not read-only.
They created, truncated, and deleted probe files under `/data/BFpilot`,
`/user/app`, and discovered mount paths. This violated the diagnostic safety
model. The probes are now removed. Write capability is reported as unknown and
the server advertises `diagReadOnly: true`.

Live writes were limited to BFpilot benchmark artifacts under
`/data/BFpilot/bench`, diagnostic logs under `/data/BFpilot`, and the explicitly
requested launcher staging directory `/user/app/BFPL00001`. No remote files
were deleted. The stable file manager remained available on port 5905.

## Architecture

- `src/lite_main.c`: boot marker, reload handoff, optional integrated launcher
  path, then the HTTP listener.
- `src/websrv_lite.c`: threaded HTTP server and `/api/status`/`/api/diag`.
- `src/fs.c`: direct download streaming through `/fs/<path>`.
- `src/transfer.c`: list/stat/du, upload, PS5-side copy/move/delete, and the
  single long-running job state.
- `assets/files.html`: browser UI. Copy and move call PS5-side API endpoints;
  files do not round-trip through the browser.
- `src/app_installer.c`: dynamically resolved integrated installer used only by
  `bfpilot-full.elf`.
- `src/launcher_installer_force_main.c`: direct AppInstUtil installer.
- `src/launcher_installer_main.c`: runtime-resolution diagnostic installer.

## Transfer Paths

### Upload

The browser sends one raw `XMLHttpRequest` body per file to
`POST /api/fs/upload`. There is no multipart parser and no chunked HTTP
support; `Content-Length` is required. Files are uploaded sequentially. The
server receives and writes each chunk directly to the final path. On an upload
failure, only the incomplete file created by that request is removed.

### Download

`src/fs.c` opens the requested file, sends a fixed `Content-Length`, then
streams it with a 64 KiB buffer. It does not use `sendfile`.

### Copy And Move

Copy is PS5-side. It performs a full recursive pre-scan for totals, copies to a
unique `.bfpilot-part-*` staging path, then renames the staging path into place.
Move first attempts `rename`; only `EXDEV` falls back to copy plus source
cleanup. Cancellation is checked between reads and recursive entries.

### Progress

There is one global file-operation job. Counters are atomic; strings and
timestamps use a short mutex. Browser polling runs every 500-700 ms in separate
HTTP client threads and does not hold the worker lock while transferring.

## Performance Root Causes

Confirmed from source:

- Copy and upload used fixed 256 KiB buffers.
- Every copied file ended with synchronous `fsync`, which can dominate copies
  involving many files or slow USB media.
- Directory copy performs two recursive walks: pre-scan and copy.
- Upload is sequential per browser file, so many small files pay one HTTP
  request/response cost each.
- Download uses a smaller 64 KiB buffer.
- There is no multipart parsing, per-chunk allocation, destination rescanning,
  or global lock around transfer I/O.

Changes:

- Copy/upload buffer is now configurable through
  `BFPILOT_TRANSFER_BUF_SIZE`, defaulting to 1 MiB.
- Removed per-file `fsync`; close and staged final rename remain checked.
- Added monotonic elapsed milliseconds, bytes read/written, average MB/s,
  current/source/destination paths, source/destination `st_dev`, return state,
  and errno.
- Added `/api/fs/transfer/stats` for the last upload and extended job status.

Live safe benchmark results are recorded in `docs/CODEX_FINAL_REPORT.md`.
Same-device internal copy reached 133-180 MiB/s while upload reached 3.46-3.92
MiB/s and download reached 5.58 MiB/s. This isolates the current throughput
limit to the LAN/client/HTTP streaming path rather than `/data` storage or the
PS5-side copy loop.

## Launcher Build Boundaries

| Target | Launcher behavior | AppInstUtil handling |
| --- | --- | --- |
| `bfpilot.elf` | none | no launcher source; no direct import |
| `bfpilot-debug.elf` | none | no launcher source; no direct import |
| `bfpilot-full.elf` | experimental integrated install | dynamic resolution via `sce_resolve.c` |
| `bfpilot-launcher-installer-safe.elf` | diagnostic/runtime path | runtime handle/symbol lookup |
| `bfpilot-launcher-installer.elf` | isolated installer | direct websrv-pattern imports: `kernel_sys`, SystemService, UserService, AppInstUtil |
| `tests/installer_linkonly_appinst.elf` | import probe | direct `-lSceAppInstUtil` |
| runtime/entry probes | diagnostics | no direct AppInstUtil import |

The tile metadata in `assets-app/param.json` points to
`http://127.0.0.1:5905/`.

The original direct installer linked only AppInstUtil. That ELF was rejected
before `main()`. Runtime handle lookup returned `0xffffffff`; both `dlopen` and
`sceSysmoduleLoadModuleInternal` stopped before returning. Comparing working
projects exposed the missing architecture:

- ps5-payload-dev/websrv links `kernel_sys`, SystemService, UserService, and
  AppInstUtil together.
- It initializes UserService and changes the process authid to
  `0x4801000000000013` before AppInst initialization.
- Elf Arsenal independently uses the same authid and documents it as required
  for AppInst initialization on firmware 11.00+.

An exact non-installing websrv-pattern probe reached `main()` live and returned
zero from UserService init, authid setup, and AppInst init. The repaired
isolated installer then returned zero from AppInst init and
`AppInstallTitleDir`, registered `BFPL00001`, and restored the original authid.
This proves the root cause was BFpilot's incomplete direct dependency and
privilege setup, not tile metadata.

Launcher refresh previously uninstalled existing/legacy tiles and removed an
invalid app tree before proving registration would succeed. That made failure
recovery worse. Those removals are now skipped. Installers log the title ID,
deep link, embedded asset sizes, PNG signature, init/result codes, and staging
results.

## Build Baseline And Results

Initial local failures:

- `make clean`: failed because `PS5_PAYLOAD_SDK` was absent.
- Public SDK source checkout was not an installed SDK.
- Binary SDK initially failed because repository
  `build-tools/llvm-config` lacked executable mode and host `ld.lld`/LLVM tool
  names were unavailable.

After downloading the public binary SDK, making `build-tools/llvm-config`
executable, and supplying host LLVM tool aliases:

- baseline `make clean all`: PASS before behavior changes.
- final `make clean all`: PASS.
- final `make inspect-imports`: PASS.
- Python syntax checks: PASS.
- `make ps5-diag`: works without requiring the SDK and safely refuses the old
  mutating `/api/diag`.

The workspace has an empty read-only `.git` mount, so changes are separated by
subsystem but actual commits could not be created.
