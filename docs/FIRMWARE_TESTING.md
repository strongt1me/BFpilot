# BFpilot Firmware Testing

Use this order on every firmware/loader combination. Do not test the launcher
installer until the file manager is stable.

Remote diagnostics are read-only unless benchmark mode is explicitly enabled.
Benchmark mode writes only timestamped BFpilot-owned files under
`/data/test/bfpilot-bench` and does not delete remote files.

## Connectivity Loss Rule

If the PS5 is reachable before a BFpilot deployment or archive test and then
`5905`, `9021`, or ping access disappears immediately after that action, treat
it as a possible payload crash, loader crash, or kernel panic until proven
otherwise. Do not assume it is a normal network issue. Record the exact binary,
build tag, endpoint or payload command, archive path, thread count, status JSON,
and last BFpilot log files that were visible before the drop. After the console
comes back, restart with read-only diagnostics before any new write test.

### 2026-06-30 large archive incident

- Build: `bfpilot-v0.3.1-test6-perf5`.
- Command: `scripts/ps5_archive_perf.py` against `<PS5_IP>:5905`.
- Archive: `/tmp/bfpilot-archive-tests/large1100m-pw.rar`, about 1.1 GiB,
  stored RAR with password `testpass`.
- Remote test root:
  `/data/BFpilot/bfpilot-archive-perf-20260630T113254Z-22970`.
- Flow completed support check, mkdir, upload, archive prepare, and one
  successful archive-status poll. Shortly after extraction started, archive
  status timed out, then ping, port `5905`, and loader port `9021` became
  unreachable.
- Treat this as a likely payload crash or kernel panic until logs can be read
  after reboot. Do not resume large RAR tests with automatic multi-threading;
  restart with read-only diagnostics, collect `/data/BFpilot/log.txt`,
  `/data/BFpilot/boot.log`, and
  `/data/BFpilot/archive-integrated/archive-worker.log`, then test a smaller
  RAR with one extraction thread before increasing size or thread count.

## 1. Build Locally

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
make inspect-imports
```

Expected outputs:

```text
bfpilot.elf
bfpilot-launcher-installer.elf
bfpilot-archive-worker.elf
```

Failure meaning: local toolchain, source, or import-boundary failure. Do not
inject until this passes.

## 2. Inject File Manager

```sh
python3 payload_sender.py "$PS5_IP" "${BF_PAYLOAD_PORT:-9021}" bfpilot.elf
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" make ps5-diag
```

Expected notification/log:

- `BFpilot BOOT`
- a `bfpilot file-manager` entry in `/data/BFpilot/boot.log`
- startup lines in `/data/BFpilot/log.txt`

Expected endpoints:

- `/api/status`: HTTP 200, `mode=file-manager`, `port=5905`,
  `diagReadOnly=true`
- `/api/fs/places`: HTTP 200, built-in places plus only actually mounted
  USB/ext drives
- `/api/diag`: HTTP 200

Failure meaning:

- No boot marker: loader rejected the ELF or failure occurred before `main()`.
- Boot marker but no status: failure after `main()` during startup/bind/listen.
- Status works but places/diag fails: HTTP API regression.

Next action: save `diagnostics/ps5-diag-*.json` and the BFpilot logs.

## 3. Confirm UI Places And Storage

Open:

```text
http://<PS5_IP>:5905/
```

Expected:

- left rail shows Root, Homebrew, Mounts, User, Data, and custom shortcuts
- no `/mnt/usb0..7` or `/mnt/ext0..7` placeholders unless the drive is really
  mounted
- the top storage summary shows free/total/used percentage for Data and each
  mounted drive
- if PS5 Settings and BFpilot disagree on free space, compare `/api/fs/places`
  fields directly: `availableBytes` is user-usable free space, `freeBytes` is
  raw filesystem free space, and `reservedBytes` is the gap between them
- custom shortcuts can be added, renamed, removed, and survive reload because
  they are stored at `/data/BFpilot/shortcuts.txt`

Failure meaning:

- Placeholder USB/ext entries: mount detection regression.
- No disk space: `statvfs`/places response regression.
- Shortcuts do not persist: `/data/BFpilot/shortcuts.txt` write/read failure.

## 4. Storage Mismatch Audit

When PS5 Settings and BFpilot disagree on free space, run the read-only storage
audit before deleting or moving anything:

```sh
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" make ps5-storage-audit
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
python3 scripts/ps5_storage_audit.py --deep --settings-free-gb 18
```

The audit saves `diagnostics/ps5-storage-audit-*.json`, records `/api/fs/places`
and `/api/fs/stat` totals, lists the main storage roots, and recursively sizes
common candidates such as `/data/homebrew`, `/data/etaHEN`, `/data/shadowmount`,
`/user/app`, `/user/appmeta`, and immediate `/data/*` entries. It uses only GET
requests.

Do not add `/mnt/shadowmnt` to totals by default. Those paths are mounted views
and can double-count the same backing files. Use `--include-shadowmnt` only when
you want to inspect the mounted view separately.

Failure meaning:

- `availableBytes` near PS5 Settings free space: BFpilot UI was previously
  showing raw free or the wrong field.
- `freeBytes` much higher than `availableBytes`: filesystem reserved/quota space
  explains at least part of the gap.
- `du` totals far below `usableUsedBytes`: hidden, inaccessible, deleted-open,
  database-accounted, or system-reserved data still needs live investigation.
- a large `/data/*`, `/user/app`, `/user/data`, or shadowmount source folder:
  inspect that path before deleting anything.

## 5. Safe Transfer Benchmark

Run only after the file manager passes:

```sh
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
BF_ALLOW_PS5_WRITE=1 \
BF_ALLOWED_REMOTE_ROOTS=/data/test/bfpilot-bench \
python3 scripts/ps5_diag.py --bench --logs
```

Expected files: timestamped source/copy folders only under
`/data/test/bfpilot-bench`.

For a broader file API smoke test, run:

```sh
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
BF_ALLOW_PS5_WRITE=1 \
make ps5-smoke
```

Expected files: a unique `/data/test/bfpilot-smoke-*` folder that is deleted by
the test after upload, download, copy, rename, move, and cleanup checks pass.

Expected results:

- upload response includes `elapsedMs`, `averageMBps`, `bytesWritten`, and
  `destinationDev`
- copy job includes bytes read/written, elapsed ms, MB/s,
  source/destination device IDs, return state, and errno
- `/api/fs/transfer/stats` reflects the last upload/copy metrics

Failure meaning:

- Slow same-device copy: copy loop or internal storage path.
- Fast same-device copy but slow upload: client/LAN/HTTP receive path.
- Slow only when device IDs differ: USB/cross-filesystem path.

Do not benchmark user files.

## 5. Archive Extraction

Run only after stages 1-4 pass. Use only small BFpilot-owned test archives
under `/data/test` unless you intentionally want to extract a specific user
archive.

Prepare an archive job through the UI `Extract` button or directly:

```sh
curl -X POST "http://$PS5_IP:${BF_WEB_PORT:-5905}/api/fs/archive/prepare" \
  --data-urlencode "src=/data/test/example.7z" \
  --data-urlencode "dst=/data/test/bfpilot-example-out" \
  --data-urlencode "password=" \
  --data-urlencode "threads=0"
curl "http://$PS5_IP:${BF_WEB_PORT:-5905}/api/fs/archive/status"
```

Use `threads=0` for the default automatic archive-engine setting. 7z auto mode
is conservatively capped at 2 effective threads. RAR is clamped to
`effectiveThreads=1` until large-archive PS5 stability is proven after the
`bfpilot-v0.3.1-test6-perf5` crash incident above. For performance diagnostics,
repeat only BFpilot-owned test archives and start with `threads=1`; do not raise
RAR thread counts until small and 1 GiB single-thread tests complete without
HTTP timeouts, payload crashes, or console reboot. Compare `averageMBps`,
`effectiveThreads`, elapsed time, `cpuUtilPercent`, the input/output timing
counters, RAR `rarMtThreadedBlocks`/`rarMtLargeBlocks`, and
`priorityRc`/`priorityErrno`. Preserve the archive log/status files for any
timeout or connectivity loss.

Interpretation:

- high `cpuUtilPercent` with low IO wait usually means decompression is the
  bottleneck.
- low `rarMtThreadedBlocks` with `effectiveThreads` above 1 means the RAR
  stream is effectively serial or forced into large-block fallback.
- low `outputWaitMBps` means destination writes are throttling extraction.
- low `inputWaitMBps` means source reads are throttling extraction.

For repeatable performance runs, use:

```sh
BF_ALLOW_PS5_WRITE=1 \
BF_ARCHIVE_LOCAL=/path/to/test.rar \
BF_ARCHIVE_PASSWORD='optional-password' \
BF_ARCHIVE_THREADS=0,1,2 \
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
make ps5-archive-perf
```

The harness writes only under a BFpilot-owned test directory, saves
`diagnostics/ps5-archive-perf-*.json`, and cleans up its own test directory by
default. Run higher thread counts only after the default matrix completes
without timeouts or reboots.

Expected endpoints:

- `/api/fs/archive/support`: HTTP 200 with supported formats and limitations.
- `/api/fs/archive/support`: `requiresInjection=false` for the integrated
  archive path.
- `/api/fs/archive/status`: briefly `state=prepared` while the integrated
  daemon picks up the job.
- `/api/fs/archive/status`: `state=done`, `percent=100`, and
  `archiveExitCode=0` after successful extraction.

Expected files:

- `/data/BFpilot/archive-integrated/status.json`
- `/data/BFpilot/archive-integrated/archive-worker.log`
- the requested destination directory only after finalization succeeds

Expected failure diagnostics:

- `7z password required`: retry with the correct password.
- `bad 7z password or encrypted-header open error`: password was supplied but
  rejected, including encrypted-header 7z files.
- `bad RAR password or checksum error`: password was supplied but rejected.
- `RAR password required or archive checksum error`: retry with the password.
- Multipart RAR extraction failures can also mean a required volume is missing;
  place all RAR volumes next to the first archive.
- `zip AES encryption is not supported yet`: use a non-AES ZIP, RAR, or 7z.
- `unsafe zip member path`: archive contains absolute or parent-directory
  paths and is intentionally refused.

Do not share `/data/BFpilot/archive-integrated/job.ini` if a password was used.
It is a local job handoff file and can contain the archive password until
another job is prepared.

Fallback: `bfpilot-archive-worker.elf` is a standalone diagnostic build of the
archive engine. Normal archive extraction should work from `bfpilot.elf`
without a second payload injection.

## 6. Launcher Installer

Run only after stages 1-5 pass:

```sh
python3 payload_sender.py "$PS5_IP" "${BF_PAYLOAD_PORT:-9021}" \
  bfpilot-launcher-installer.elf
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
python3 scripts/ps5_diag.py --logs
```

Expected:

- `BFpilot BOOT` marker for `bfpilot-launcher-installer`
- `/data/BFpilot/launcher-installer.log`
- log line confirming title `BFPL00001`
- log line confirming deep link `http://127.0.0.1:5905/`
- UserService init return code
- authid set/restore return codes
- AppInst init return code
- asset/staging results
- final `AppInstallTitleDir` return code

Failure meaning:

- No boot marker/log: loader rejected the installer before `main()`.
- AppInst unavailable/init failed: AppInstUtil context not available on this
  firmware/loader.
- Authid setup failed: privilege operation unavailable.
- Asset/staging failed: title file path or embedded metadata problem.
- Install call failed: AppInst rejected the title directory.
- Tile installed but opens the wrong URL: compare `param.json` and the logged
  deep link.

## 7. Files To Save

For every failure, save:

```text
/data/BFpilot/boot.log
/data/BFpilot/log.txt
/data/BFpilot/crash.log
/data/BFpilot/launcher-installer.log
/data/BFpilot/archive-integrated/status.json
/data/BFpilot/archive-integrated/archive-worker.log
/data/BFpilot/archive/status.json
/data/BFpilot/archive/archive-worker.log
diagnostics/ps5-diag-*.json
```

Record the exact command, endpoint, HTTP status or return code, and the newest
log lines around the failure.
