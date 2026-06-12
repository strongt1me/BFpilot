# BFpilot Firmware Testing

Use this order on every firmware/loader combination. Do not skip ahead to the
launcher. Record the exact payload, endpoint, HTTP status/return code, expected
marker, actual result, and relevant source subsystem.

Remote diagnostics are read-only. Remote write tests require:

```sh
export BF_ALLOW_PS5_WRITE=1
export BF_ALLOWED_REMOTE_ROOTS=/data/BFpilot/bench
```

Never delete remote benchmark artifacts during diagnostics. They have unique
timestamped BFpilot-owned names and can be reviewed before any later cleanup.

## 1. Build Locally

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
make inspect-imports
```

Expected: all ELFs build; import inspection reports compatibility checks passed.

Failure meaning: source/toolchain/import boundary failure. Do not inject.

## 2. Stable File Manager

```sh
python3 payload_sender.py "$PS5_IP" "${BF_PAYLOAD_PORT:-9021}" bfpilot.elf
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" make ps5-diag
```

Expected notification/log: `BFpilot BOOT`; a new `bfpilot file-manager` line in
`/data/BFpilot/boot.log`; server startup lines in `/data/BFpilot/log.txt`.

Expected endpoints: `/api/status` and `/api/diag` return HTTP 200;
`/api/status` reports `mode=file-manager`, `port=5905`, and
`diagReadOnly=true`.

Failure meaning:

- No boot marker: loader rejected the ELF or failure occurred before `main()`.
- Boot marker but no status: failure after `main()` during startup/bind/listen.
- Status works but diagnostics fail: diagnostic route defect.

Next action: inject the debug payload only if this stage fails.

## 3. Debug File Manager

```sh
python3 payload_sender.py "$PS5_IP" "${BF_PAYLOAD_PORT:-9021}" bfpilot-debug.elf
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" make ps5-diag
```

Expected: `bfpilot-debug debug` boot marker and stronger startup notifications.

Failure meaning: use `/data/BFpilot/boot.log`, `/data/BFpilot/log.txt`, and
`/data/BFpilot/crash.log` to locate the last checkpoint.

## 4. Safe Transfer Benchmark

Run only after the stable file manager passes and writes are explicitly
authorized:

```sh
PS5_IP="$PS5_IP" BF_WEB_PORT="${BF_WEB_PORT:-5905}" \
BF_ALLOW_PS5_WRITE=1 \
BF_ALLOWED_REMOTE_ROOTS=/data/BFpilot/bench \
python3 scripts/ps5_diag.py --bench
```

Expected files: unique timestamped source/copy folders only under
`/data/BFpilot/bench`.

Expected results: upload response contains `elapsedMs`, `averageMBps`, and
`destinationDev`; copy job contains bytes read/written, elapsed ms, MB/s,
source/destination device IDs, and errno.

Failure meaning:

- Slow client upload and slow server upload metric: network/browser/receive or
  storage path.
- Fast server upload metric but slow client elapsed time: browser/network.
- Slow same-device copy: copy/storage path.
- Slow only when device IDs differ: USB/cross-filesystem path.

Next action: save the generated diagnostics JSON. Do not benchmark user files.

## 5. Launcher Diagnostic Order

Only continue after stages 1-4 are stable.

### 5.1 Safe Installer

Inject `bfpilot-launcher-installer-safe.elf`.

Expected: boot marker and `/data/BFpilot/launcher-installer.log`.

Failure meaning: no marker means failure before `main()` unrelated to direct
AppInst import. A log showing AppInstUtil unavailable means runtime resolution
cannot install on this environment.

### 5.2 Entry Probe

Inject `tests/installer_enter_probe.elf`.

Expected file: `/data/BFpilot/installer_enter_probe.txt`.

Failure meaning: loader rejected even the safe installer-shaped probe.

### 5.3 Direct Import Probe

Inject `tests/installer_linkonly_appinst.elf`.

Expected file: `/data/BFpilot/linkonly_appinst_entered.txt`.

Failure meaning: absent marker after the entry probe passed means direct
AppInstUtil imports were rejected before `main()`.

### 5.4 Runtime Resolve Probe

Inject `tests/installer_runtime_resolve_appinst.elf`.

Expected files: `/data/BFpilot/runtime_resolve_entered.txt` and
`/data/BFpilot/runtime_resolve_appinst.log`.

Failure meaning: `kernel_dynlib_handle ... rc=0xffffffff` means AppInstUtil is
not reachable through the safe runtime path.

### 5.5 Exact Websrv-Pattern Probe

Build and inject `tests/installer_websrv_pattern.elf`:

```sh
make installer-websrv-pattern
python3 payload_sender.py "$PS5_IP" "${BF_PAYLOAD_PORT:-9021}" \
  tests/installer_websrv_pattern.elf
```

Expected files: `/data/BFpilot/websrv_pattern_entered.txt` and
`/data/BFpilot/websrv_pattern.log`. Expected return codes for UserService init,
authid setup, and AppInst init are all zero.

Failure meaning: the loader/firmware does not support the complete dependency
and privilege composition used by the isolated installer. Do not attempt tile
registration.

### 5.6 Isolated Installer

Inject `bfpilot-launcher-installer.elf` only when the websrv-pattern probe
passes.

Expected: boot marker, launcher log, metadata line showing title
`BFPL00001` and `http://127.0.0.1:5905/`, asset validation, AppInst init code,
staging codes, install code, and final result.

Failure meaning:

- No boot marker: the complete direct-import set was rejected before `main()`.
- Init failed: AppInstUtil present but unusable in the current context.
- Authid setup failed: `kernel_sys` privilege operation unavailable.
- Asset/staging failed: path permissions or invalid/missing assets.
- Registration failed: AppInst install call rejected metadata/title directory.
- Installed but wrong URL: compare staged `param.json` with the logged deep link.

### 5.7 Experimental Full Build

Inject `bfpilot-full.elf` last.

Expected: launcher diagnostics plus a functioning web server even if launcher
registration fails.

Failure meaning: integrated dynamic path is incompatible with the environment.
Continue using `bfpilot.elf`; do not merge installer imports into it.
