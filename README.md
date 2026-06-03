# BFpilot

BFpilot is a small PS5 payload that starts a browser-based file manager on port
`5905`.

It is meant to stay simple: no PKG installer, no FTP daemon, no mounting tools,
no payload bundle, no exploit code, and no DRM or backup features. The
compatibility-first build starts one standalone web server and lets the PS5 do
file operations locally.

Current prepared release: `v0.2.1`

## Recommended Build

Use `bfpilot-core.elf` first on every new firmware or loader.

`bfpilot-core.elf` is the recommended build for maximum firmware compatibility:

- Starts the HTTP file manager on `http://<PS5_IP>:5905/`
- Does not install or refresh the PS5 launcher tile
- Does not compile launcher installer code
- Does not link launcher-only SCE libraries
- Keeps notification attempts optional

Only try `bfpilot-full.elf` after `bfpilot-core.elf` works on the same firmware
and loader.

`bfpilot-full.elf` includes optional launcher tile support. Launcher install may
fail on some firmware, exploit, or loader combinations. That should not stop the
file manager. If launcher install fails but port `5905` still works, use the web
file manager directly from a browser.

## What It Does

- Runs a file-manager web UI at `http://<PS5_IP>:5905/`
- Builds `bfpilot-core.elf` for the safest firmware compatibility path
- Builds `bfpilot-full.elf` for optional PS5 home-screen launcher support
- Browses, uploads, downloads, copies, moves, renames, creates folders, and
  deletes
- Shows progress for copy, move, delete, and browser uploads
- Reports size, speed, ETA, item count, and current path/file while working
- Calculates folder size only when you select a folder
- Creates missing copy/move target folders automatically
- Keeps the default target shortcuts at `/data/homebrew` and `/mnt/usb0`
- Writes diagnostics to `/data/BFpilot/log.txt`
- Writes fatal-signal diagnostics to `/data/BFpilot/crash.log`

The full launcher tile opens:

```text
http://127.0.0.1:5905/
```

Injecting the payload does not auto-open the browser. Core mode only starts the
web server. Full mode starts the web server and then attempts launcher install or
refresh as optional work.

## Runtime Notes

Inject the ELF to your payload loader on port `9021`.

For first tests on a firmware:

```text
send bfpilot-core.elf to <PS5_IP>:9021
open http://<PS5_IP>:5905/
open http://<PS5_IP>:5905/api/status
open http://<PS5_IP>:5905/api/diag
```

If core works and you want the PS5 launcher tile:

```text
send bfpilot-full.elf to <PS5_IP>:9021
open http://<PS5_IP>:5905/
open http://<PS5_IP>:5905/api/diag
```

Full mode can skip launcher work at runtime:

```text
--no-launcher
BFPILOT_NO_LAUNCHER=1
```

The full build stores launcher marker data in:

```text
/data/BFpilot
```

The PS5 launcher app itself is installed under:

```text
/user/app/BFPL00001
```

If launcher install fails, keep testing the web server. The file manager can
still work without a launcher tile.

## Diagnostics

Use these endpoints while testing firmware compatibility:

```text
http://<PS5_IP>:5905/api/status
http://<PS5_IP>:5905/api/diag
```

Save these files when reporting failures:

```text
/data/BFpilot/log.txt
/data/BFpilot/crash.log
```

### `/api/diag` Core Example

This is the shape of a healthy core-mode response. Values such as `pid`,
`uptime`, `cwd`, and permission booleans vary by firmware and loader.

```json
{
  "ok": true,
  "name": "BFpilot",
  "tag": "bfpilot-v0.2.1",
  "version": "dev",
  "mode": "core",
  "pid": 112,
  "uptime": 14,
  "port": 5905,
  "cwd": "/",
  "can_stat_root": true,
  "can_opendir_data": true,
  "can_opendir_data_homebrew": true,
  "can_opendir_usb0": true,
  "can_write_data_bfpilot": true,
  "can_write_user_app": false,
  "launcher_status": "not_compiled",
  "last_errno": 0,
  "checkpoint": "web server ready",
  "core_server_started": true,
  "launcher_attempted": false,
  "appinst_init_rc": -2147483000,
  "title_dir_resolved": false,
  "install_title_rc": -2147483000,
  "install_all_rc": -2147483000,
  "uninstall_rc": -2147483000,
  "launcher_final_state": "skipped",
  "rcs": {
    "sceNetCtlInit": -2147483000,
    "sceUserServiceInitialize": -2147483000,
    "notification_test": 0,
    "bind_5905": 0,
    "listen_5905": 0
  },
  "launcher": {
    "status": "not_compiled",
    "status_rc": -2147483000,
    "compiled": false,
    "disabled": true,
    "launcher_enabled": false,
    "launcher_attempted": false,
    "appinst_init_rc": -2147483000,
    "title_dir_resolved": false,
    "install_title_dir_resolved": false,
    "install_title_rc": -2147483000,
    "uninstall_resolved": false,
    "uninstall_rc": -2147483000,
    "install_all_resolved": false,
    "install_all_rc": -2147483000,
    "user_app_writable": null,
    "launcher_install_rc": -1,
    "launcher_final_state": "skipped"
  },
  "routes": ["/", "/api/status", "/api/diag", "/fs", "/api/fs/*"]
}
```

### `/api/diag` Full Launcher-Failed Example

This example is still acceptable if the web server works. The important part is
that launcher failure is recorded and the server remains reachable.

```json
{
  "ok": true,
  "name": "BFpilot",
  "tag": "bfpilot-v0.2.1",
  "mode": "full",
  "port": 5905,
  "can_opendir_data": true,
  "can_write_data_bfpilot": true,
  "launcher_status": "failed_nonfatal",
  "core_server_started": true,
  "launcher_attempted": true,
  "appinst_init_rc": -2,
  "title_dir_resolved": false,
  "install_title_rc": -2147483000,
  "install_all_rc": -2147483000,
  "uninstall_rc": -2147483000,
  "launcher_final_state": "failed_nonfatal",
  "rcs": {
    "notification_test": -1,
    "bind_5905": 0,
    "listen_5905": 0
  },
  "launcher": {
    "status": "failed_nonfatal",
    "status_rc": -1,
    "compiled": true,
    "disabled": false,
    "launcher_enabled": true,
    "launcher_attempted": true,
    "appinst_init_rc": -2,
    "title_dir_resolved": false,
    "install_title_dir_resolved": false,
    "install_title_rc": -2147483000,
    "uninstall_resolved": false,
    "uninstall_rc": -2147483000,
    "install_all_resolved": true,
    "install_all_rc": -2147483000,
    "user_app_writable": false,
    "launcher_install_rc": -2,
    "launcher_final_state": "failed_nonfatal"
  }
}
```

## Troubleshooting

### Port `5905` Not Reachable

- Confirm the payload sender reported a successful send to port `9021`.
- Try `bfpilot-core.elf` before `bfpilot-full.elf`.
- Check whether another BFpilot instance already owns port `5905`.
- Open `/data/BFpilot/log.txt` and look for `bind_5905`, `listen_5905`, and
  `web server listening`.
- Recheck the PS5 IP address and make sure the browser device is on the same
  network.

### Payload Exits Immediately

- Use `bfpilot-core.elf` first.
- Check the sender console for loader-side errors.
- Check `/data/BFpilot/crash.log` for fatal signal details.
- Check `/data/BFpilot/log.txt` for the last checkpoint.
- If reinjecting during a copy, move, or delete job, the new instance may exit
  intentionally so the old job is not killed.

### Launcher Install Failed

- This is not fatal in `v0.2.1`.
- Confirm `http://<PS5_IP>:5905/` still opens.
- Open `/api/diag` and record `launcher_attempted`, `appinst_init_rc`,
  `title_dir_resolved`, `install_title_rc`, `install_all_rc`, `uninstall_rc`,
  `launcher_final_state`, `launcher.user_app_writable`, and
  `launcher.launcher_install_rc`.
- If the file manager works, keep using the browser URL and report the launcher
  diagnostics separately.

### `/mnt/usb0` Missing

- Confirm the external drive is connected before starting BFpilot.
- Try reconnecting the drive and refreshing the file manager.
- Check `/api/diag` for `can_opendir_usb0`.
- Some setups may expose a different USB mount path. Include `/api/diag` and
  `/data/BFpilot/log.txt` in the report.

### Upload Fails

- Test with a tiny `.txt` file first.
- Upload to `/data/BFpilot-test` before testing USB storage.
- Confirm `/api/diag` reports `can_write_data_bfpilot: true`.
- If upload to USB fails but upload to `/data` works, treat it as a USB
  permission or mount-path issue.
- Save the browser error and the latest `/data/BFpilot/log.txt` lines.

### Copy/Delete Permission Denied

- Test inside `/data/BFpilot-test` first.
- Avoid deleting root paths or system paths.
- For folder deletion, make sure recursive delete is explicitly requested by the
  UI/API.
- If `/data` works but USB fails, collect `/api/diag` and note the source and
  destination paths.
- Permission errors are useful compatibility data; record exact paths and the
  operation that failed.

## Firmware Testing

Use [docs/FIRMWARE_TESTING.md](docs/FIRMWARE_TESTING.md) for the full
stage-by-stage protocol:

- Stage A: core server only
- Stage B: notification enabled
- Stage C: launcher enabled
- Stage D: reinjection

## Project Layout

- `src/lite_main.c` starts the payload, handles reinjection handoff, applies the
  launcher runtime flag, and starts the web server.
- `src/app_installer.c` writes launcher files and optionally registers the PS5
  home-screen tile in full mode.
- `src/sce_resolve.c` resolves optional SCE functions for full-mode launcher
  support.
- `src/diag.c` writes append logs, crash logs, checkpoints, and runtime
  diagnostics.
- `src/websrv_lite.c` serves the UI, downloads under `/fs`, `/api/status`,
  `/api/diag`, and file-manager APIs.
- `src/transfer.c` contains upload, copy, move, delete, and long-running job
  handling.
- `assets/files.html` is the full web UI that gets embedded into the ELF.
- `assets/icon.png` is the smaller web UI icon.
- `assets-app/` contains the launcher tile metadata and icon.

Generated files go under `gen/` during build. Build intermediates go under
`build/`. Release ELF files should be attached to GitHub Releases only after the
release checklist is complete.

## Build

You need:

- `ps5-payload-sdk`
- GNU `make`
- Python 3
- LLVM/Clang tools that work with the PS5 payload SDK
- A shell that can run the SDK build commands

Build both release files:

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
```

Build one mode:

```sh
make core
make full
```

Outputs:

```text
bfpilot-core.elf
bfpilot-full.elf
```

Deploy helper:

```sh
make deploy-core PS5_HOST=<PS5_IP> PS5_PORT=9021
make deploy-full PS5_HOST=<PS5_IP> PS5_PORT=9021
```

## Extra Notes

BFpilot avoids kernel-level cleanup and does not force-unload unrelated
payloads.

If you reinject while an older BFpilot or BS5FileManager instance is already
running, the new payload checks whether a file job is active. If the old
instance is idle, it asks it to shut down cleanly and then takes over port
`5905`. If a copy, move, or delete is running, the new injection exits instead
of interrupting the active job.

Cancel is cooperative. Large operations may take a moment to stop, especially on
external USB drives, but the job code checks for cancellation during scan, copy,
move cleanup, and delete.

## Release Checklist

Complete this before publishing `v0.2.1`:

- Confirm `VERSION_TAG` is `bfpilot-v0.2.1`.
- Build from clean with `make clean all`.
- Confirm both files exist: `bfpilot-core.elf` and `bfpilot-full.elf`.
- Confirm `bfpilot-core.elf` is the recommended download in release notes.
- Confirm core mode does not include launcher installer objects.
- Confirm core mode does not link launcher-only SCE libraries.
- Confirm full mode treats launcher failure as non-fatal.
- Run Stage A from `docs/FIRMWARE_TESTING.md` on each available firmware.
- Run Stage C only after Stage A passes on that firmware.
- Save `/api/diag` output for tested firmware/loader combinations.
- Save `/data/BFpilot/log.txt` for failed tests.
- Fill in the compatibility table in `docs/FIRMWARE_TESTING.md`.
- Attach both ELFs to the release.
- In the release notes, tell users to try `bfpilot-core.elf` first.
