# BFpilot Final Report

## Root Causes And Fixes

Copy performance was code-limited by 256 KiB buffers and a forced `fsync` after
every copied file. Copy/upload now use configurable 1 MiB buffers, copy no
longer forces per-file `fsync`, and transfer diagnostics report bytes,
elapsed-ms, MB/s, paths, device IDs, return state, and errno through job status,
upload responses, logs, and `/api/fs/transfer/stats`.

The tile failure was architectural. BFpilot linked AppInstUtil alone and omitted
the working-project service/privilege context. Live testing proved:

- AppInst-only direct import was rejected before `main()`.
- Safe runtime lookup returned `0xffffffff`.
- Runtime `dlopen` and Sysmodule loading stopped before returning.
- The exact ps5 websrv pattern reached `main()` and AppInst init returned `0`.

`bfpilot-launcher-installer.elf` now directly links `kernel_sys`,
SystemService, UserService, and AppInstUtil; initializes UserService; sets
authid `0x4801000000000013`; performs non-destructive staging/registration; and
restores its original authid before exit. Live `AppInstallTitleDir` returned
`0`, installing `BFPL00001` with `http://127.0.0.1:5905/`.

## Payload Choice

- `bfpilot.elf`: stable file manager only.
- `bfpilot-debug.elf`: file manager startup proof.
- `bfpilot-launcher-installer.elf`: proven isolated tile installer.
- `bfpilot-launcher-installer-safe.elf`: read-only AppInst availability probe.
- `bfpilot-full.elf`: experimental integrated dynamic path; not the recommended installer.

## Live Measurements

| Test | Result |
| --- | --- |
| 8 MiB upload | 3.46 MiB/s server, 2.331 s client |
| 8 MiB same-device copy | 133.33 MiB/s, 60 ms |
| 32 MiB upload | 3.92 MiB/s server, 8.183 s client |
| 32 MiB download | 5.58 MiB/s client |
| 32 MiB same-device copy | 179.78 MiB/s, 178 ms |

Internal copy is 34-46 times faster than upload. `/data` storage and PS5-side
copy are not the current upload bottleneck; the limiting path is LAN/client/HTTP
streaming. Browser folder upload remains sequential per file.

## Exact Commands

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
make clean all
make inspect-imports

python3 payload_sender.py 192.168.1.204 9021 bfpilot.elf
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 make ps5-diag

python3 payload_sender.py 192.168.1.204 9021 bfpilot-launcher-installer.elf
PS5_IP=192.168.1.204 BF_WEB_PORT=5905 python3 scripts/ps5_diag.py --logs
```

## Remaining Limitations

- Results prove the repaired installer on the tested firmware/loader, not every
  firmware in existence.
- Download still uses a 64 KiB userspace loop without `sendfile`.
- Recursive directory copy pre-scans for accurate progress.
- USB write throughput was not benchmarked.
- `bfpilot-full.elf` remains less compatible than the isolated installer.
