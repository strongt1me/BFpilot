#!/usr/bin/env python3
"""Read-only BFpilot storage accounting audit."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


DEFAULT_STAT_PATHS = [
    "/data",
    "/user",
    "/system_data",
    "/mnt",
    "/data/BFpilot",
    "/data/bfpilot",
    "/data/homebrew",
    "/data/etaHEN",
    "/data/shadowmount",
    "/data/test",
    "/user/app",
    "/user/appmeta",
    "/user/data",
    "/mnt/ext0",
    "/mnt/ext1",
    "/mnt/usb0",
    "/mnt/usb1",
    "/mnt/usb2",
    "/mnt/usb3",
    "/mnt/usb4",
    "/mnt/usb5",
    "/mnt/usb6",
    "/mnt/usb7",
    "/mnt/shadowmnt",
    "/mnt/shadowmnt/pfsc",
]

DEFAULT_DU_PATHS = [
    "/data/BFpilot",
    "/data/bfpilot",
    "/data/homebrew",
    "/data/etaHEN",
    "/data/shadowmount",
    "/data/test",
    "/user/app",
    "/user/appmeta",
    "/user/data",
]

DEEP_DU_PATHS = [
    "/data",
    "/user",
    "/system_data",
]

SHADOW_DU_PATHS = [
    "/mnt/shadowmnt",
    "/mnt/shadowmnt/pfsc",
]


def q(value: str) -> str:
    return urllib.parse.quote(value, safe="")


def fs_url(base: str, path: str) -> str:
    return base + "/fs" + urllib.parse.quote(path, safe="/") + "?fmt=json"


def human_size(value: object) -> str:
    try:
        size = float(value)
    except (TypeError, ValueError):
        return "n/a"
    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    unit = 0
    while abs(size) >= 1024.0 and unit < len(units) - 1:
        size /= 1024.0
        unit += 1
    if unit == 0:
        return f"{size:.0f} {units[unit]}"
    return f"{size:.2f} {units[unit]}"


def request_json(base: str, path: str, timeout: int) -> tuple[bool, object, int | None, int]:
    url = base + path
    started = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            raw = response.read()
            elapsed_ms = round((time.monotonic() - started) * 1000)
            body = json.loads(raw)
            ok = response.status == 200
            print(("PASS" if ok else "FAIL") + f" GET {url} HTTP {response.status} {elapsed_ms}ms")
            return ok, body, response.status, elapsed_ms
    except urllib.error.HTTPError as exc:
        elapsed_ms = round((time.monotonic() - started) * 1000)
        try:
            body: object = json.loads(exc.read())
        except (OSError, ValueError):
            body = {"ok": False, "error": str(exc)}
        print(f"SKIP GET {url}: HTTP {exc.code} {elapsed_ms}ms")
        return False, body, exc.code, elapsed_ms
    except (OSError, ValueError, urllib.error.URLError) as exc:
        elapsed_ms = round((time.monotonic() - started) * 1000)
        print(f"FAIL GET {url}: {exc}", file=sys.stderr)
        return False, {"ok": False, "url": url, "error": str(exc)}, None, elapsed_ms


def fetch_dir(base: str, path: str, timeout: int) -> tuple[bool, object, int | None, int]:
    url = fs_url(base, path)
    started = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            raw = response.read()
            elapsed_ms = round((time.monotonic() - started) * 1000)
            body = json.loads(raw)
            ok = response.status == 200 and isinstance(body, list)
            print(("PASS" if ok else "FAIL") + f" GET {url} HTTP {response.status} {elapsed_ms}ms")
            return ok, body, response.status, elapsed_ms
    except urllib.error.HTTPError as exc:
        elapsed_ms = round((time.monotonic() - started) * 1000)
        print(f"SKIP GET {url}: HTTP {exc.code} {elapsed_ms}ms")
        return False, {"ok": False, "http": exc.code}, exc.code, elapsed_ms
    except (OSError, ValueError, urllib.error.URLError) as exc:
        elapsed_ms = round((time.monotonic() - started) * 1000)
        print(f"FAIL GET {url}: {exc}", file=sys.stderr)
        return False, {"ok": False, "url": url, "error": str(exc)}, None, elapsed_ms


def stat_path(base: str, path: str, timeout: int) -> dict[str, object]:
    ok, body, http, elapsed_ms = request_json(
        base, "/api/fs/stat?path=" + q(path), timeout
    )
    return {
        "ok": ok,
        "path": path,
        "http": http,
        "elapsedMs": elapsed_ms,
        "body": body,
    }


def du_path(base: str, path: str, timeout: int) -> dict[str, object]:
    ok, body, http, elapsed_ms = request_json(
        base, "/api/fs/du?path=" + q(path), timeout
    )
    return {
        "ok": ok,
        "path": path,
        "http": http,
        "elapsedMs": elapsed_ms,
        "body": body,
    }


def list_path(base: str, path: str, timeout: int) -> dict[str, object]:
    ok, body, http, elapsed_ms = fetch_dir(base, path, timeout)
    return {
        "ok": ok,
        "path": path,
        "http": http,
        "elapsedMs": elapsed_ms,
        "body": body,
    }


def unique_paths(paths: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for path in paths:
        path = path.rstrip("/") or "/"
        if path not in seen:
            seen.add(path)
            out.append(path)
    return out


def top_level_data_targets(data_listing: object) -> list[str]:
    if not isinstance(data_listing, list):
        return []
    targets: list[str] = []
    for entry in data_listing:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name") or "")
        if not name or "/" in name or name in (".", ".."):
            continue
        targets.append("/data/" + name)
    return targets


def body_dict(item: dict[str, object]) -> dict[str, object]:
    body = item.get("body")
    return body if isinstance(body, dict) else {}


def print_summary(report: dict[str, object], settings_free_gb: float | None) -> None:
    stats = report.get("stats")
    if isinstance(stats, dict):
        data_stat = stats.get("/data")
        if isinstance(data_stat, dict):
            data_body = body_dict(data_stat)
            if data_body.get("ok") is not False and data_body:
                available = data_body.get("availableBytes")
                free_raw = data_body.get("freeBytes")
                reserved = data_body.get("reservedBytes")
                usable_total = data_body.get("usableTotalBytes")
                usable_used = data_body.get("usableUsedBytes")
                print("")
                print("Data filesystem:")
                print(f"  usable free: {human_size(available)}")
                print(f"  raw free:    {human_size(free_raw)}")
                print(f"  reserved:    {human_size(reserved)}")
                print(f"  usable used: {human_size(usable_used)} of {human_size(usable_total)}")
                if settings_free_gb is not None:
                    settings = settings_free_gb * 1024.0 * 1024.0 * 1024.0
                    try:
                        gap = float(available) - settings
                        print(f"  Settings free comparison gap: {human_size(gap)}")
                    except (TypeError, ValueError):
                        pass

    du_items: list[tuple[int, str, dict[str, object]]] = []
    du = report.get("du")
    if isinstance(du, dict):
        for path, item in du.items():
            if not isinstance(item, dict):
                continue
            body = body_dict(item)
            if body.get("ok") is False or "size" not in body:
                continue
            try:
                size = int(body.get("size", 0))
            except (TypeError, ValueError):
                continue
            du_items.append((size, path, body))
    if du_items:
        print("")
        print("Largest measured paths:")
        for size, path, body in sorted(du_items, reverse=True)[:20]:
            entries = body.get("entries", "?")
            elapsed = "?"
            item = du.get(path) if isinstance(du, dict) else None
            if isinstance(item, dict):
                elapsed = item.get("elapsedMs", "?")
            print(f"  {human_size(size):>10}  {path}  entries={entries} elapsedMs={elapsed}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect read-only BFpilot storage accounting diagnostics."
    )
    parser.add_argument("--deep", action="store_true",
                        help="also recurse /data, /user, and /system_data")
    parser.add_argument("--include-shadowmnt", action="store_true",
                        help="also measure shadowmount mounted views; do not sum with source roots")
    parser.add_argument("--no-top-data", action="store_true",
                        help="skip recursive size checks for immediate /data entries")
    parser.add_argument("--timeout", type=int, default=180,
                        help="per-request timeout in seconds")
    parser.add_argument("--initial-timeout", type=int, default=8,
                        help="initial /api/status timeout in seconds")
    parser.add_argument("--settings-free-gb", type=float, default=None,
                        help="PS5 Settings free-space value to compare against")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    host = os.environ.get("PS5_IP", "192.168.1.204")
    port = os.environ.get("BF_WEB_PORT", "5905")
    base = f"http://{host}:{port}"
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_dir = pathlib.Path("diagnostics")
    out_dir.mkdir(exist_ok=True)

    report: dict[str, object] = {
        "timestampUtc": stamp,
        "baseUrl": base,
        "readOnly": True,
        "notes": [
            "This audit only uses BFpilot GET endpoints.",
            "Do not sum /mnt/shadowmnt with source folders unless you intentionally want mounted views.",
        ],
    }

    status_ok, status, status_http, status_elapsed = request_json(
        base, "/api/status", args.initial_timeout
    )
    report["status"] = {
        "ok": status_ok,
        "http": status_http,
        "elapsedMs": status_elapsed,
        "body": status,
    }
    if not status_ok:
        output = out_dir / f"ps5-storage-audit-{stamp}.json"
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"saved {output}")
        return 1

    places_ok, places, places_http, places_elapsed = request_json(
        base, "/api/fs/places", args.timeout
    )
    report["places"] = {
        "ok": places_ok,
        "http": places_http,
        "elapsedMs": places_elapsed,
        "body": places,
    }

    list_roots = ["/data", "/user", "/system_data", "/mnt", "/data/shadowmount"]
    listings = {path: list_path(base, path, args.timeout) for path in list_roots}
    report["listings"] = listings

    stat_paths = unique_paths(DEFAULT_STAT_PATHS)
    report["stats"] = {path: stat_path(base, path, args.timeout) for path in stat_paths}

    du_paths = list(DEFAULT_DU_PATHS)
    if not args.no_top_data:
        data_listing = listings.get("/data", {})
        if isinstance(data_listing, dict):
            du_paths.extend(top_level_data_targets(data_listing.get("body")))
    if args.deep:
        du_paths.extend(DEEP_DU_PATHS)
    if args.include_shadowmnt:
        du_paths.extend(SHADOW_DU_PATHS)
    du_paths = unique_paths(du_paths)
    report["duPaths"] = du_paths
    report["du"] = {path: du_path(base, path, args.timeout) for path in du_paths}

    output = out_dir / f"ps5-storage-audit-{stamp}.json"
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"saved {output}")
    print_summary(report, args.settings_free_gb)
    return 0 if status_ok and places_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
