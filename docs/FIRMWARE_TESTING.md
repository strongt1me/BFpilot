# BFpilot Firmware Testing

Use this protocol on every firmware/loader combination. Test the file manager
first. Test launcher/AppInstUtil only after the file manager works.

Record:

- Firmware version.
- Exploit/loader name and version.
- Payload name.
- Whether `/data/BFpilot/boot.log` received a new entry.
- Whether a notification appeared.
- Whether the HTTP server started.
- `/api/status` and `/api/diag` output when reachable.
- Relevant log files under `/data/BFpilot`.

## Stage A: Main File Manager

1. Send `bfpilot.elf` to the loader on port `9021`.
2. Verify `BFpilot BOOT` notification if notifications are working.
3. Verify `/data/BFpilot/boot.log` has a new `bfpilot file-manager` entry.
4. Open `http://PS5_IP:5905/`.
5. Open `http://PS5_IP:5905/api/status`.
6. Open `http://PS5_IP:5905/api/diag`.
7. Browse `/data`.
8. Browse `/mnt/usb0` if an external drive is connected.
9. Upload a small text file to `/data/BFpilot-test`.
10. Copy, move, and delete that test file.
11. Save `/data/BFpilot/log.txt`.

Pass criteria:

- Boot marker appears.
- Server starts on port `5905`.
- `/`, `/api/status`, `/api/diag`, `/fs`, and `/api/fs/*` are reachable.
- Basic file operations work in `/data/BFpilot-test`.

## Stage B: Debug File Manager

Use this when Stage A appears to do nothing or when testing a new loader.

1. Send `bfpilot-debug.elf` to the loader on port `9021`.
2. Verify `/data/BFpilot/boot.log` has a new `bfpilot-debug debug` entry.
3. Open `http://PS5_IP:5905/api/status`.
4. Open `http://PS5_IP:5905/api/diag`.
5. Save `/data/BFpilot/log.txt`.

Expected result:

- If `boot.log` has no new entry, the payload likely failed before `main()` or
  the loader rejected it.
- If `boot.log` has an entry but `/api/status` is unreachable, the failure is
  after `main()` and before or during server startup.

## Stage C: Alternate Port

Use this when another BFpilot instance may already be running on `5905`.

1. Send `bfpilot-debug.elf --port 5906` to the loader.
2. Open `http://PS5_IP:5906/api/status`.
3. Open `http://PS5_IP:5906/api/diag`.
4. Save `/data/BFpilot/log.txt`.

Pass criteria:

- The new payload can start on `5906` without touching the old server on `5905`.

## Stage D: Reinjection

1. Start `bfpilot.elf`.
2. Reinject `bfpilot.elf` while idle.
3. Verify notification `BFpilot reload: old server detected`.
4. Verify the old listener stops cleanly and the new server starts.
5. Start a large copy or move.
6. Reinject `bfpilot.elf` during the active job.
7. Verify notification `BFpilot reload blocked`.
8. Verify the old job is not killed.
9. Save `/data/BFpilot/log.txt`.

Pass criteria:

- Idle reinjection performs a clean handoff.
- Active file jobs are not interrupted.
- Reload exits are logged and notified instead of appearing silent.

## Stage E: Launcher Installer

Only run this after Stage A passes on the same firmware/loader.

### Integrated Full Build

This is the first installer-capable build to try on firmware 11.6, because it
preserves the 0.2.0-style launcher flow that already installed there.

1. Send `bfpilot-full.elf` to the loader on port `9021`.
2. Watch for `BFpilot BOOT`, `BFpilot app`, and `BFpilot started`
   notifications.
3. Open `http://PS5_IP:5905/`.
4. Save `/data/BFpilot/log.txt`.
5. Open `http://PS5_IP:5905/api/diag` if the web server starts.
6. Check whether the BFpilot tile appears or refreshes.

Pass criteria:

- On firmware where AppInstUtil runtime resolution works, the tile installs or
  refreshes.
- If launcher install fails, the web server still starts and logs the exact
  AppInst resolution/init/install return codes.

### Isolated Direct Installer

Use this after Stage A if you want to test the direct AppInst import pattern
used by websrv/Payload Manager/ftpsrv. It may fail before `main()` if the loader
rejects AppInstUtil imports.

1. Send `bfpilot-launcher-installer.elf` to the loader on port `9021`.
2. Watch for `BFpilot BOOT` and launcher installer notifications.
3. Check whether the BFpilot tile appears or refreshes.
4. Save `/data/BFpilot/launcher-installer.log`.
5. Save `/data/BFpilot/boot.log`.

Launcher installer fields to record from the log:

- `entered main`.
- `AppInst init return code`.
- `/user/app/BFPL00001` mkdir return code.
- `param.json` and `icon0.png` write return code.
- `AppInstallTitleDir resolved`.
- `AppInstallTitleDir return code`.
- `AppInstallAll fallback return code`.
- `final result`.

Pass criteria:

- Launcher install may succeed or fail.
- Launcher failure does not affect `bfpilot.elf`.
- File manager remains testable through `http://PS5_IP:5905/`.

If the direct installer gives no notification and no log:

1. Send `bfpilot-launcher-installer-safe.elf`.
2. Save `/data/BFpilot/launcher-installer.log`.
3. If the safe installer reaches `main()` but logs
   `kernel_dynlib_handle libSceAppInstUtil.sprx rc=0xffffffff`, AppInstUtil is
   not available through the safe runtime path.
4. If `tests/installer_linkonly_appinst.elf` also produces no marker, direct
   AppInst imports fail before `main()` on that loader/firmware.

In that case the direct installer is being rejected before it can run, while
the main file manager remains usable through `http://PS5_IP:5905/`.

## Stage F: Minimal Probe Payloads

Use these to isolate loader, notification, HTTP, and AppInstUtil behavior.

### `tests/hello_boot.elf`

1. Send `tests/hello_boot.elf`.
2. Verify boot marker notification.
3. Verify `/data/BFpilot/boot.log`.
4. Wait 10 seconds for `BFpilot BOOT still alive`.

### `tests/hello_http.elf`

1. Send `tests/hello_http.elf`.
2. Open `http://PS5_IP:5906/api/status`.

### `tests/hello_notify.elf`

1. Send `tests/hello_notify.elf`.
2. Record notification result from sender output and `/data/BFpilot/boot.log`.

### `tests/installer_enter_probe.elf`

1. Send `tests/installer_enter_probe.elf`.
2. Verify `/data/BFpilot/installer_enter_probe.txt` exists.
3. If it does not exist, the loader rejected even the safe installer-shaped
   probe before `main()`.

### `tests/installer_linkonly_appinst.elf`

1. Send `tests/installer_linkonly_appinst.elf`.
2. Verify `/data/BFpilot/linkonly_appinst_entered.txt` exists.
3. If it does not exist, direct AppInstUtil linking is incompatible with that
   loader/firmware and installer code must avoid direct AppInst imports.

### `tests/installer_runtime_resolve_appinst.elf`

1. Send `tests/installer_runtime_resolve_appinst.elf`.
2. Verify `/data/BFpilot/runtime_resolve_entered.txt` exists.
3. Save `/data/BFpilot/runtime_resolve_appinst.log`.
4. Use the log to see whether `kernel_dynlib_handle`, symbol lookup, and NID
   resolve reached AppInstUtil.

Interpretation:

- `kernel_dynlib_handle ... rc=0xffffffff` means AppInstUtil is not already
  loaded in this process.
- BFpilot intentionally skips `dlopen` for AppInstUtil after a prior test stops
  before the `dlopen` result log, because that path is not safe enough for a
  compatibility payload.

## Compatibility Table

| firmware | exploit/loader | payload used | boot marker | server starts | file browse | upload | copy | move | delete | launcher install | notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12.70 |  | bfpilot.elf |  |  |  |  |  |  |  | n/a |  |
| 12.70 |  | bfpilot-full.elf |  |  |  |  |  |  |  |  |  |
| 12.70 |  | bfpilot-launcher-installer.elf |  | n/a | n/a | n/a | n/a | n/a | n/a |  |  |
| 11.60 |  | bfpilot.elf |  |  |  |  |  |  |  | n/a |  |
| 11.60 |  | bfpilot-full.elf |  |  |  |  |  |  |  |  |  |
| 11.60 |  | bfpilot-launcher-installer.elf |  | n/a | n/a | n/a | n/a | n/a | n/a |  |  |
