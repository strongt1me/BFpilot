# BFpilot Optimization Changelog

This document outlines the modifications, optimizations, and new features introduced in this optimized version of **BFpilot** compared to the original payload.

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

## 3. Networking Optimization (`sendfile` & `TCP_NODELAY`)
* **Files:** `src/fs.c` and `src/websrv_lite.c`
* **Changes:**
  * Replaced the traditional `read()` / `write()` buffer loop used for file downloads with the FreeBSD `sendfile()` syscall. This allows the PS5 kernel to stream files directly from the SSD to the network socket.
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
