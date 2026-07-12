# BFpilot Optimization Changelog

This document outlines the modifications, optimizations, and new features introduced in this optimized version of **BFpilot** compared to the original payload.

## v0.3.8 — Test Build Clean (2026-07-12)

### New Features

* **Conflict Overwrite & Merge Prompts** (`src/transfer.c`, `assets/files.html`):
  * Copy and Move operations now detect destination naming conflicts before starting the background task.
  * Returns a `409 Conflict` response if the target exists and overwrite is not requested.
  * The web UI shows a confirm dialog: "Overwrite?" for files or "Merge?" for folders.
  * Selecting Skip continues to the next file in the selection queue without halting the transfer.
  * Selecting Overwrite/Merge retries the operation with `overwrite=1`, unlinking conflicting files or merging into existing folders.

* **Checkbox Multi-Selection** (`assets/files.html`):
  * Every file and folder row now has an individual checkbox for toggle selection, making multi-select easy with PS5 controllers.
  * A "Select All" master checkbox in the column header selects or deselects all items in the current folder simultaneously.
  * Clicking the checkbox area toggles the item without deselecting the rest; clicking the row body still does single-select as before.

* **Interactive Image Viewer** (`assets/files.html`):
  * Built-in image viewer for all major formats: `.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.webp`, `.ico`, `.svg`.
  * Supports grab-and-drag panning, mouse-wheel zooming (0.1× – 10×), 90° rotation, and a reset button.
  * Escape key closes the viewer; double-clicking an image row opens it directly.
  * The toolbar View/Edit button dynamically switches label based on selection (image → "View", text file → "Edit").

* **Multi-Log Viewer with PS5 Error History Decoder** (`assets/files.html`):
  * The log button now shows a dropdown selector with prioritized log sources:
    * Bfpilot server log, Archive extractor log, Search crawler log, Boot log.
    * PS5 VSH system log.
    * PS5 Error History — crawls `/system_data/priv/error/history/`, decodes the JSON crash records (error codes, Title IDs, firmware version, ticks), and displays them as a readable trace list.
  * Auto-refreshes every 2 seconds while the overlay is open.

### Improvements

* **Permission Bypass** (`src/lite_main.c`, `src/transfer.c`): Global `umask(0)` at process start plus `chmod`/`fchmod` overrides to `0777` on all created files, directories, and uploads.
* **Split ZIP Support** (`src/archive_worker.cpp`): Full support for both official (`.z01`/`.zip`) and raw (`.zip.001`/`.zip.002`) multi-volume split ZIP archives.
* **Extract Dialog Default Target** (`assets/files.html`): The extract dialog now pre-fills the destination from the active Target panel path.
* **Search Simplified to Index All**: Removed the separate "Index System" mode; unified into a single "Index All" crawler that covers all storage roots.

---

## v0.3.5 — Test Build 10 Clean (2026-07-05)

### Key Features

* **Graceful Exit & Re-injection**: Loopback TCP wakeup, clean background thread shutdown, instant port release.
* **Fast Multi-Threaded Search Indexing**: Work-stealing concurrent crawler thread pool, 0ms query resolution.
* **Integrated Archive Extraction**: RAR, 7z (including split `.7z.001`), ZIP — all extracted inside the main ELF.
* **Premium Web UI & Virtual Scrolling**: Row-recycling virtual scroller prevents OOM on 10,000+ file directories.
* **Storage & Network Optimizations**: FreeBSD `sendfile`, 2 MB socket buffers, `TCP_NODELAY`.

---

## 1. Web UI Overhaul (Dark Mode & Virtual Scrolling)
* **File:** `assets/files.html`
* **Changes:**
  * Replaced the table-based frontend with a high-performance dark mode user interface.
  * Implemented **Search Highlighting**: Search terms are dynamically highlighted in both filenames and directory paths using `<mark>` tags.
  * Implemented **Virtual Scrolling**: The DOM recycles rows to prevent the PS5 Web Browser from running out of memory (OOM) when opening directories or search results with over 10,000+ files.

## 2. SSD Crawler Optimization (opendir/readdir thread safety)
* **File:** `src/search.c`
* **Changes:**
  * Implemented a fast multi-threaded search directory crawler with work-stealing queues.
  * Ensured thread-safe traversal by using standard POSIX `opendir` and `readdir` descriptors per worker thread.

## 3. Networking Optimization (`TCP_NODELAY` & bounded reads)
* **Files:** `src/fs.c` and `src/websrv_lite.c`
* **Changes:**
  * Tuned sockets with `TCP_NODELAY` and kept bounded read/write streaming for downloads, including tail reads for large log files.
  * Added `TCP_NODELAY` to eliminate Nagle's algorithm and boosted socket send/receive buffers (`SO_SNDBUF` and `SO_RCVBUF`) to 2MB. This improves overall webserver responsiveness and maximizes file transfer speeds.

## 4. Search Engine Memory Footprint Halved
* **File:** `src/search.c` and `src/search.h`
* **Changes:**
  * Removed the `lower` string field from `search_entry_t`. The original codebase cached a lowercase copy of every single filepath in memory to perform case-insensitive searches.
  * Switched to using `strcasestr()` from the PS5 SDK (`string.h`), which performs fast case-insensitive substring matching directly on the original path. This cuts the RAM usage of the search index by 50%.

## 5. Process Shutdown & Re-injection Stability
* **Files:** `src/lite_main.c` and `src/websrv_lite.c`
* **Changes:**
  * Replaced standard `exit(0)` calls with `_exit(0)` to prevent the PS5 from hanging during payload termination due to runtime library cleanups.
  * Implemented loopback TCP wakeup to unblock `accept()` calls instantly on shutdown.
  * Added graceful termination flags and routines to stop the background archive daemon and search crawler threads before process exit, allowing instant re-injection without console reboots.

## 6. Build System & Windows Compatibility
* **Files:** `Makefile` and `gen-asset-module.py`
* **Changes:**
  * Replaced the Unix-only `xxd` shell pipeline with a custom Python script (`gen-asset-module.py`) to compile `files.html` into a C header `files_html.h`.
  * Cleaned up the Makefile to allow standard building on Windows using Git Bash or command-line make.
