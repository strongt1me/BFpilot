# BFpilot Compatibility Strategy

BFpilot now separates the stable file manager from the fragile launcher
installer path.

## Payload Roles

### `bfpilot.elf`

This is the main release payload. It is firmware-light and should be tested
first on every firmware and loader.

- Starts the HTTP file manager on port `5905`.
- Does not install a launcher tile.
- Does not include `src/app_installer.c`.
- Does not link `libSceAppInstUtil`.
- Does not depend on AppInstUtil, SystemService, UserService, or launcher-only
  Sony libraries.

Broad firmware support comes from avoiding imports that can fail before
`main()` on some loaders or firmware combinations.

### `bfpilot-debug.elf`

This is the same file-manager payload with extra debug notifications/logging.
It still avoids AppInstUtil and launcher installer code.

Use it when `bfpilot.elf` appears to do nothing. If the debug payload writes
`/data/BFpilot/boot.log`, it reached `main()`.

### `bfpilot-full.elf`

This build restores the 0.2.0-style integrated launcher flow for firmware and
loaders where that path worked.

- Starts BFpilot from the same file-manager process.
- Runs optional `sceNetCtlInit` and `sceUserServiceInitialize` resolution/init.
- Dynamically resolves AppInstUtil symbols instead of direct-linking
  AppInstUtil.
- Attempts `/user/app/BFPL00001` install/refresh before the web server loop.
- Continues to the web server if launcher install fails.

Use this first when specifically testing home-screen tile install on firmware
11.6, because it is closest to the previous full build that installed there.

### `bfpilot-launcher-installer.elf`

This optional payload installs or refreshes `/user/app/BFPL00001`.

It is intentionally isolated from the file manager:

- It direct-links AppInstUtil, matching the launcher-install pattern used by
  websrv, Payload Manager, and ftpsrv.
- It also links `kernel_sys`, SystemService, and UserService, initializes
  UserService, temporarily sets authid `0x4801000000000013`, and restores the
  original authid after registration.
- It logs to `/data/BFpilot/launcher-installer.log`.
- It cannot break the file manager because it is not part of `bfpilot.elf`.

If `bfpilot.elf` works but the launcher installer fails, the issue is the
AppInstUtil/launcher path, not the file manager.

### `bfpilot-launcher-installer-safe.elf`

This diagnostic payload avoids direct AppInstUtil imports and checks whether
AppInstUtil is already reachable through runtime symbol lookup. It is not the
force install path. Use it after the direct installer gives no notification or
log.

## Launcher/AppInst Findings

The current probes separate three cases:

- `tests/installer_enter_probe.elf` writes a marker with safe imports only.
  If this works, the loader can run ordinary BFpilot-shaped ELFs.
- `tests/installer_linkonly_appinst.elf` directly imports AppInstUtil but does
  not call it. If this payload does not write its marker, the loader or
  firmware rejected the ELF before `main()`.
- `tests/installer_runtime_resolve_appinst.elf` has safe imports only and then
  checks whether `libSceAppInstUtil.sprx` is already loaded. If
  `kernel_dynlib_handle` returns `0xffffffff`, AppInstUtil is not reachable
  through the safe runtime path.

The AppInst-only direct probe can fail even when the complete websrv pattern
works. `tests/installer_websrv_pattern.elf` reproduces the full dependency and
privilege setup without registering a title. On the tested live PS5 it reached
`main()` and returned zero from AppInst initialization; the repaired isolated
installer then registered the tile successfully.

## Patterns Checked

`ps5-payload-dev/websrv`, Payload Manager by itsPLK, and elf-arsenal launcher
installers all use the same basic AppInst pattern:

- link `libSceAppInstUtil` directly;
- call `sceAppInstUtilInitialize()` directly;
- resolve only `sceAppInstUtilAppInstallTitleDir` by NID `Wudg3Xe3heE`;
- fall back to `sceAppInstUtilAppInstallAll()`.

That pattern can work in environments where direct AppInst imports are accepted,
but it can also fail before `main()` on loaders that reject or cannot satisfy
the import. BFpilot keeps this pattern out of `bfpilot.elf`.

Working payloads depend on a system authid before AppInst init can succeed on
newer firmware. BFpilot implements that only in the isolated launcher installer.
The stable file manager does not change authid and does not import launcher
services.

## Boot Marker

Every ELF calls:

```c
bfpilot_boot_marker(payload_name, build_mode);
```

as the first meaningful line in `main()`.

The marker:

- Prints `BFpilot BOOT: <payload_name> <build_mode> entered main`.
- Tries to create `/data/BFpilot`.
- Appends payload name, build mode, PID, and build version to
  `/data/BFpilot/boot.log`.
- Attempts a notification titled `BFpilot BOOT`.
- Continues if notification fails.

If no notification appears and no `/data/BFpilot/boot.log` entry exists, the
payload probably failed before `main()` or the loader rejected the ELF. If the
boot marker exists but the server does not start, the crash or hang is after
`main()`.

## Reload Behavior

Startup is explicit:

1. Boot marker runs.
2. BFpilot checks whether the selected port already has an old BFpilot server.
3. If an old server is found, BFpilot notifies `BFpilot reload: old server detected`.
4. BFpilot asks the old server to shut down through `/api/control/shutdown`.
5. If shutdown succeeds, the new server starts.
6. If shutdown fails or a file job is busy, BFpilot notifies `BFpilot reload blocked`,
   logs the exact reason to `/data/BFpilot/log.txt`, and exits cleanly.

This avoids the misleading case where reinjection looks like nothing happened.

## Alternate Port

The default web port is compile-time configurable with `BFPILOT_WEB_PORT`.

Runtime port override is supported:

```text
--port 5906
```

Use this to test a new build while an old BFpilot instance is still running on
`5905`.

## Import Inspection

Run:

```sh
make inspect-imports
```

The target prints file size, dynamic libraries/imports, undefined symbols, and
SCE-related symbol/string hints for:

- `bfpilot.elf`
- `bfpilot-debug.elf`
- `bfpilot-full.elf`
- `bfpilot-launcher-installer.elf`
- `bfpilot-launcher-installer-safe.elf`
- `tests/hello_boot.elf`
- `tests/hello_http.elf`
- `tests/hello_notify.elf`
- `tests/installer_enter_probe.elf`
- `tests/installer_linkonly_appinst.elf`
- `tests/installer_runtime_resolve_appinst.elf`

It fails if `bfpilot.elf` or `bfpilot-debug.elf` contain:

- `SceAppInstUtil`
- `sceAppInst`
- `app_installer`
- `BFPL00001`

Only installer/probe payloads may directly import AppInstUtil. The release
installer must additionally import `kernel_sys`, SystemService, and UserService.
The link-only test intentionally proves that an incomplete AppInst-only
composition can be rejected before `main()`.
