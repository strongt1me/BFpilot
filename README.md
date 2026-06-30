# BFpilot

BFpilot is a lightweight PS5 payload that serves a browser-based file manager at
`http://<PS5_IP>:5905/`.

The project is split into three payloads:

- `bfpilot.elf` - the main file manager payload with integrated archive
  extraction.
- `bfpilot-launcher-installer.elf` - an optional home-screen tile installer.
- `bfpilot-archive-worker.elf` - a fallback diagnostic archive worker.

The main payload stays compatibility-focused. It does not import launcher
installer libraries, does not install the tile, and can be used without
touching PS5 app installation services.

## Features

- Browse local PS5 paths from a web browser.
- Upload and download files.
- Copy, move, rename, create folders, and delete from the web UI.
- PS5-side copy and move operations with progress and cancellation.
- Transfer timing, throughput, and device diagnostics.
- Extract RAR, 7z, split 7z, and ZIP archives from the main file manager ELF.
- Clean places sidebar for Root, Homebrew, Mounts, User, Data, mounted drives,
  and custom shortcuts.
- Mounted USB/ext drives are shown only when they are actually present.
- Top storage summary for Data and mounted drives.
- Persistent custom shortcuts with add, rename, and remove controls.
- Optional launcher tile that opens `http://127.0.0.1:5905/`.

## Download

Use the release assets:

- `bfpilot.elf` for the file manager and archive extraction.
- `bfpilot-launcher-installer.elf` only if you want the PS5 home-screen tile.
- `bfpilot-archive-worker.elf` only as a fallback diagnostic archive worker.

Run `bfpilot.elf` first. It serves the web UI and starts the integrated archive
daemon; archive extraction does not require injecting another payload. Run the
launcher installer payload only if you want the tile.

## Usage

Inject the file manager payload to your loader, usually on port `9021`:

```sh
python3 payload_sender.py <PS5_IP> 9021 bfpilot.elf
```

Open:

```text
http://<PS5_IP>:5905/
```

Health checks:

```text
http://<PS5_IP>:5905/api/status
http://<PS5_IP>:5905/api/diag
http://<PS5_IP>:5905/api/fs/places
```

Install or refresh the launcher tile:

```sh
python3 payload_sender.py <PS5_IP> 9021 bfpilot-launcher-installer.elf
```

The tile opens:

```text
http://127.0.0.1:5905/
```

## Archive Extraction

Archive extraction is integrated into `bfpilot.elf`. The file manager prepares
jobs, starts a lightweight archive daemon, and reports progress through the web
API. `bfpilot-archive-worker.elf` is kept as a fallback diagnostic build of the
same archive engine.

From the web UI:

1. Select one archive file.
2. Click `Extract`.
3. Choose a destination under `/data` or a mounted USB/ext drive.
4. Enter a password if the archive needs one.
5. Watch the progress panel.

The UI polls:

```text
http://<PS5_IP>:5905/api/fs/archive/status
```

Supported today:

- RAR, including passworded archives and normal multipart RAR sets when every
  part is present beside the first volume.
- 7z, passworded 7z, and `.7z.001` split sets.
- ZIP stored/deflate entries, ZIP64 sizes/offsets, and traditional ZipCrypto
  passwords.

Extraction uses conservative archive-engine threading by default. The archive
API accepts `threads=0` for automatic tuning. 7z auto mode is currently capped
at 2 effective threads; RAR is clamped to 1 effective thread until large-archive
PS5 stability is proven after the `perf5` crash investigation. Status responses
include `threads`, `effectiveThreads`, `threadMode`, elapsed time, average MB/s,
input/output timing counters, and best-effort archive priority fields. RAR
status also reports thread-pool counters such as `rarMtThreadedBlocks` and
`rarMtLargeBlocks` for diagnostics, but normal RAR extraction should report
`effectiveThreads=1` for now.

Known limits:

- ZIP AES encryption reports unsupported.
- Split ZIP is not implemented yet.
- Archive jobs cannot be cancelled from the browser after extraction starts.
- Failed extractions leave the BFpilot-owned staging folder in place for
  inspection instead of deleting evidence.

## Build

Set `PS5_PAYLOAD_SDK` to your PS5 payload SDK path:

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

`make inspect-imports` verifies that the file manager payload does not contain
launcher/AppInstUtil imports and that the isolated installer contains the
required installer imports. The file manager may include archive engine code,
but it must remain launcher-free.

## Diagnostics

Local read-only diagnostics:

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-diag
```

Read-only storage accounting audit for Settings/BFpilot free-space mismatches:

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 make ps5-storage-audit
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 \
python3 scripts/ps5_storage_audit.py --deep --settings-free-gb 18
```

Smoke test the file APIs using only BFpilot-created files under `/data/test`:

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 \
BF_ALLOW_PS5_WRITE=1 \
make ps5-smoke
```

Optional benchmark mode writes under `/data/test/bfpilot-bench` by default:

```sh
PS5_IP=<PS5_IP> BF_WEB_PORT=5905 \
BF_ALLOW_PS5_WRITE=1 \
BF_ALLOWED_REMOTE_ROOTS=/data/test/bfpilot-bench \
python3 scripts/ps5_diag.py --bench
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

The harness uploads the archive once under a BFpilot-owned test folder,
extracts to separate destinations for each requested thread count, saves
`diagnostics/ps5-archive-perf-*.json`, captures archive logs, and deletes only
its own test folder by default. Use manual thread counts above 2 only for
controlled stability testing.

Runtime logs are stored on the PS5 at:

```text
/data/BFpilot/boot.log
/data/BFpilot/log.txt
/data/BFpilot/crash.log
/data/BFpilot/launcher-installer.log
/data/BFpilot/archive-integrated/archive-worker.log
/data/BFpilot/archive-integrated/status.json
/data/BFpilot/archive/archive-worker.log
/data/BFpilot/archive/status.json
```

## Compatibility Notes

`bfpilot.elf` is launcher-free by design. It avoids AppInstUtil,
SystemService, UserService, and privileged launcher imports because those can
fail before `main()` on some firmware/loader combinations. Archive extraction
is integrated into the main ELF, but only through libc/network-safe archive
code and the BFpilot-owned job/status files under
`/data/BFpilot/archive-integrated`.

`bfpilot-launcher-installer.elf` is separate and uses the complete launcher
installer dependency set. If launcher installation fails on a firmware, the file
manager payload remains usable.

More detail is in [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md)
and [docs/FIRMWARE_TESTING.md](docs/FIRMWARE_TESTING.md).
