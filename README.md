# BFpilot

BFpilot is a lightweight PS5 payload that serves a browser-based file manager at
`http://<PS5_IP>:5905/`.

The project is split into three payloads:

- `bfpilot.elf` - the main file manager payload with built-in archive extraction.
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
- Extract ZIP, 7z, split 7z, and RAR archives from the main file manager ELF.
- Clean places sidebar for Root, Homebrew, Mounts, User, Data, mounted drives,
  and custom shortcuts.
- Mounted USB/ext drives are shown only when they are actually present.
- Top storage summary for Data and mounted drives.
- Persistent custom shortcuts with add, rename, and remove controls.
- Optional launcher tile that opens `http://127.0.0.1:5905/`.

## Download

Use the release assets:

- `bfpilot.elf` for the file manager.
- `bfpilot-launcher-installer.elf` only if you want the PS5 home-screen tile.
- `bfpilot-archive-worker.elf` only for archive diagnostics or fallback testing.

Run `bfpilot.elf` first. After the web UI is working, run the launcher installer
payload if you want the tile.

## Usage

Inject the file manager payload to your loader, usually on port `9021`:

```sh
python3 payload_sender.py 192.168.1.204 9021 bfpilot.elf
```

Open:

```text
http://192.168.1.204:5905/
```

Health checks:

```text
http://192.168.1.204:5905/api/status
http://192.168.1.204:5905/api/diag
http://192.168.1.204:5905/api/fs/places
```

Install or refresh the launcher tile:

```sh
python3 payload_sender.py 192.168.1.204 9021 bfpilot-launcher-installer.elf
```

The tile opens:

```text
http://127.0.0.1:5905/
```

## Archive Extraction

Archive extraction is built into `bfpilot.elf`. The payload starts a small
archive daemon child before the web server begins accepting threaded HTTP
requests, so normal extraction does not require injecting another ELF.

From the web UI:

1. Select one archive file.
2. Click `Extract`.
3. Choose a destination under `/data` or a mounted USB/ext drive.
4. Enter a password if the archive needs one.
5. Watch the progress panel.

The UI polls:

```text
http://192.168.1.204:5905/api/fs/archive/status
```

Supported today:

- RAR, including passworded archives and normal multipart RAR sets when every
  part is present beside the first volume.
- 7z, passworded 7z, and `.7z.001` split sets.
- ZIP stored/deflate entries, ZIP64 sizes/offsets, and traditional ZipCrypto
  passwords.

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
required installer imports.

## Diagnostics

Local read-only diagnostics:

```sh
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 make ps5-diag
```

Smoke test the file APIs using only BFpilot-created files under `/data/test`:

```sh
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 \
BF_ALLOW_PS5_WRITE=1 \
make ps5-smoke
```

Optional benchmark mode writes under `/data/test/bfpilot-bench` by default:

```sh
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 \
BF_ALLOW_PS5_WRITE=1 \
BF_ALLOWED_REMOTE_ROOTS=/data/test/bfpilot-bench \
python3 scripts/ps5_diag.py --bench
```

Runtime logs are stored on the PS5 at:

```text
/data/BFpilot/boot.log
/data/BFpilot/log.txt
/data/BFpilot/crash.log
/data/BFpilot/launcher-installer.log
/data/bfpilot/archive/archive-worker.log
/data/bfpilot/archive/status.json
```

## Compatibility Notes

`bfpilot.elf` is launcher-free by design. It avoids AppInstUtil,
SystemService, UserService, and privileged launcher imports because those can
fail before `main()` on some firmware/loader combinations. Archive extraction
is included in the file manager ELF and is isolated in a daemon child process.

`bfpilot-launcher-installer.elf` is separate and uses the complete launcher
installer dependency set. If launcher installation fails on a firmware, the file
manager payload remains usable.

More detail is in [docs/COMPATIBILITY_STRATEGY.md](docs/COMPATIBILITY_STRATEGY.md)
and [docs/FIRMWARE_TESTING.md](docs/FIRMWARE_TESTING.md).
