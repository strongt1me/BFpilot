# BFpilot - reproducible PS5 payload builds.

SHELL := bash

ifeq ($(strip $(PS5_PAYLOAD_SDK)),)
$(error PS5_PAYLOAD_SDK is required, e.g. export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

HOST_UNAME := $(shell uname -s 2>/dev/null || echo unknown)
HOST_IS_WINDOWS := 0
ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(HOST_UNAME)))
HOST_IS_WINDOWS := 1
endif

PS5_HOST ?= ps5
PS5_PORT ?= 9021
PYTHON ?= python3
WEB_PORT ?= 5905
HELLO_HTTP_PORT ?= 5906

VERSION_TAG := bfpilot-v0.2.1
BUILD_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

LLVM_BINDIR ?= $(shell dirname "$$(command -v clang 2>/dev/null || command -v clang.exe 2>/dev/null || command -v llvm-strip 2>/dev/null || command -v llvm-strip.exe 2>/dev/null || echo clang)" 2>/dev/null || echo .)
LLVM_CONFIG ?= $(CURDIR)/build-tools/llvm-config
export LLVM_BINDIR
export LLVM_CONFIG

BFPILOT_BIN := bfpilot.elf
DEBUG_BIN := bfpilot-debug.elf
FULL_BIN := bfpilot-full.elf
LAUNCHER_INSTALLER_BIN := bfpilot-launcher-installer.elf
SAFE_LAUNCHER_INSTALLER_BIN := bfpilot-launcher-installer-safe.elf
HELLO_BOOT_BIN := tests/hello_boot.elf
HELLO_HTTP_BIN := tests/hello_http.elf
HELLO_NOTIFY_BIN := tests/hello_notify.elf
INSTALLER_ENTER_PROBE_BIN := tests/installer_enter_probe.elf
INSTALLER_LINKONLY_APPINST_BIN := tests/installer_linkonly_appinst.elf
INSTALLER_RUNTIME_RESOLVE_APPINST_BIN := tests/installer_runtime_resolve_appinst.elf

WEB_SRCS := src/lite_main.c
WEB_SRCS += src/boot_marker.c
WEB_SRCS += src/diag.c
WEB_SRCS += src/websrv_lite.c
WEB_SRCS += src/asset.c
WEB_SRCS += src/fs.c
WEB_SRCS += src/mime.c
WEB_SRCS += src/notify.c
WEB_SRCS += src/transfer.c

LEGACY_FULL_SRCS := $(WEB_SRCS)
LEGACY_FULL_SRCS += src/app_installer.c
LEGACY_FULL_SRCS += src/sce_resolve.c

LAUNCHER_INSTALLER_SRCS := src/launcher_installer_force_main.c
LAUNCHER_INSTALLER_SRCS += src/boot_marker.c
LAUNCHER_INSTALLER_SRCS += src/notify.c

SAFE_LAUNCHER_INSTALLER_SRCS := src/launcher_installer_main.c
SAFE_LAUNCHER_INSTALLER_SRCS += src/boot_marker.c
SAFE_LAUNCHER_INSTALLER_SRCS += src/notify.c
SAFE_LAUNCHER_INSTALLER_SRCS += src/sce_resolve.c

HELLO_BOOT_SRCS := tests/hello_boot.c src/boot_marker.c src/notify.c
HELLO_HTTP_SRCS := tests/hello_http.c src/boot_marker.c src/notify.c
HELLO_NOTIFY_SRCS := tests/hello_notify.c src/boot_marker.c src/notify.c
INSTALLER_ENTER_PROBE_SRCS := tests/installer_enter_probe.c
INSTALLER_ENTER_PROBE_SRCS += src/boot_marker.c
INSTALLER_ENTER_PROBE_SRCS += src/notify.c
INSTALLER_LINKONLY_APPINST_SRCS := tests/installer_linkonly_appinst.c
INSTALLER_LINKONLY_APPINST_SRCS += src/boot_marker.c
INSTALLER_LINKONLY_APPINST_SRCS += src/notify.c
INSTALLER_RUNTIME_RESOLVE_APPINST_SRCS := tests/installer_runtime_resolve_appinst.c
INSTALLER_RUNTIME_RESOLVE_APPINST_SRCS += src/boot_marker.c
INSTALLER_RUNTIME_RESOLVE_APPINST_SRCS += src/notify.c

ASSETS := $(wildcard assets/*)
GEN_SRCS := $(patsubst assets/%,gen/assets/%,$(ASSETS:=.c))
APP_ASSETS := assets-app/param.json assets-app/icon0.png

BFPILOT_OBJS := $(patsubst %.c,build/bfpilot/%.o,$(WEB_SRCS) $(GEN_SRCS))
DEBUG_OBJS := $(patsubst %.c,build/debug/%.o,$(WEB_SRCS) $(GEN_SRCS))
LEGACY_FULL_OBJS := $(patsubst %.c,build/legacy-full/%.o,$(LEGACY_FULL_SRCS) $(GEN_SRCS))
LAUNCHER_INSTALLER_OBJS := $(patsubst %.c,build/launcher-installer/%.o,$(LAUNCHER_INSTALLER_SRCS))
SAFE_LAUNCHER_INSTALLER_OBJS := $(patsubst %.c,build/launcher-installer-safe/%.o,$(SAFE_LAUNCHER_INSTALLER_SRCS))
HELLO_BOOT_OBJS := $(patsubst %.c,build/hello-boot/%.o,$(HELLO_BOOT_SRCS))
HELLO_HTTP_OBJS := $(patsubst %.c,build/hello-http/%.o,$(HELLO_HTTP_SRCS))
HELLO_NOTIFY_OBJS := $(patsubst %.c,build/hello-notify/%.o,$(HELLO_NOTIFY_SRCS))
INSTALLER_ENTER_PROBE_OBJS := $(patsubst %.c,build/installer-enter-probe/%.o,$(INSTALLER_ENTER_PROBE_SRCS))
INSTALLER_LINKONLY_APPINST_OBJS := $(patsubst %.c,build/installer-linkonly-appinst/%.o,$(INSTALLER_LINKONLY_APPINST_SRCS))
INSTALLER_RUNTIME_RESOLVE_APPINST_OBJS := $(patsubst %.c,build/installer-runtime-resolve-appinst/%.o,$(INSTALLER_RUNTIME_RESOLVE_APPINST_SRCS))

COMMON_CFLAGS := -Os -Wall -Werror -Isrc
COMMON_CFLAGS += -ffunction-sections -fdata-sections -flto
COMMON_CFLAGS += -DVERSION_TAG=\"$(VERSION_TAG)\"
COMMON_CFLAGS += -DBUILD_VERSION=\"$(BUILD_VERSION)\"
COMMON_CFLAGS += -DBFPILOT_SDK_PATH=\"$(PS5_PAYLOAD_SDK)\"

BFPILOT_CFLAGS := $(COMMON_CFLAGS)
BFPILOT_CFLAGS += -DBFPILOT_PAYLOAD_NAME=\"bfpilot\"
BFPILOT_CFLAGS += -DBFPILOT_BUILD_MODE=\"file-manager\"
BFPILOT_CFLAGS += -DBFPILOT_WEB_PORT=$(WEB_PORT)
BFPILOT_CFLAGS += -DBFPILOT_DEBUG_NOTIFICATIONS=0
BFPILOT_CFLAGS += -DBFPILOT_ENABLE_LAUNCHER=0
BFPILOT_CFLAGS += -DBFPILOT_DISABLE_LAUNCHER=1

DEBUG_CFLAGS := $(COMMON_CFLAGS)
DEBUG_CFLAGS += -DBFPILOT_PAYLOAD_NAME=\"bfpilot-debug\"
DEBUG_CFLAGS += -DBFPILOT_BUILD_MODE=\"debug\"
DEBUG_CFLAGS += -DBFPILOT_WEB_PORT=$(WEB_PORT)
DEBUG_CFLAGS += -DBFPILOT_DEBUG_NOTIFICATIONS=1
DEBUG_CFLAGS += -DBFPILOT_ENABLE_LAUNCHER=0
DEBUG_CFLAGS += -DBFPILOT_DISABLE_LAUNCHER=1

FULL_CFLAGS := $(COMMON_CFLAGS)
FULL_CFLAGS += -DBFPILOT_PAYLOAD_NAME=\"bfpilot-full\"
FULL_CFLAGS += -DBFPILOT_BUILD_MODE=\"full\"
FULL_CFLAGS += -DBFPILOT_WEB_PORT=$(WEB_PORT)
FULL_CFLAGS += -DBFPILOT_DEBUG_NOTIFICATIONS=1
FULL_CFLAGS += -DBFPILOT_ENABLE_LAUNCHER=1
FULL_CFLAGS += -DBFPILOT_DISABLE_LAUNCHER=0

LAUNCHER_INSTALLER_CFLAGS := $(COMMON_CFLAGS)
LAUNCHER_INSTALLER_CFLAGS += -DBFPILOT_PAYLOAD_NAME=\"bfpilot-launcher-installer\"
LAUNCHER_INSTALLER_CFLAGS += -DBFPILOT_BUILD_MODE=\"launcher-installer-force\"

SAFE_LAUNCHER_INSTALLER_CFLAGS := $(COMMON_CFLAGS)
SAFE_LAUNCHER_INSTALLER_CFLAGS += -DBFPILOT_PAYLOAD_NAME=\"bfpilot-launcher-installer-safe\"
SAFE_LAUNCHER_INSTALLER_CFLAGS += -DBFPILOT_BUILD_MODE=\"launcher-installer-safe\"

HELLO_BOOT_CFLAGS := $(COMMON_CFLAGS)
HELLO_BOOT_CFLAGS += -DBFPILOT_BUILD_MODE=\"hello-boot\"

HELLO_HTTP_CFLAGS := $(COMMON_CFLAGS)
HELLO_HTTP_CFLAGS += -DBFPILOT_BUILD_MODE=\"hello-http\"
HELLO_HTTP_CFLAGS += -DBFPILOT_HELLO_HTTP_PORT=$(HELLO_HTTP_PORT)

HELLO_NOTIFY_CFLAGS := $(COMMON_CFLAGS)
HELLO_NOTIFY_CFLAGS += -DBFPILOT_BUILD_MODE=\"hello-notify\"

INSTALLER_ENTER_PROBE_CFLAGS := $(COMMON_CFLAGS)
INSTALLER_ENTER_PROBE_CFLAGS += -DBFPILOT_BUILD_MODE=\"installer-enter-probe\"

INSTALLER_LINKONLY_APPINST_CFLAGS := $(COMMON_CFLAGS)
INSTALLER_LINKONLY_APPINST_CFLAGS += -DBFPILOT_BUILD_MODE=\"installer-linkonly-appinst\"

INSTALLER_RUNTIME_RESOLVE_APPINST_CFLAGS := $(COMMON_CFLAGS)
INSTALLER_RUNTIME_RESOLVE_APPINST_CFLAGS += -DBFPILOT_BUILD_MODE=\"installer-runtime-resolve-appinst\"

COMMON_LDFLAGS := -Wl,--gc-sections -flto
APPINST_LDLIBS := -lSceAppInstUtil

CC_CMD := "$(CC)"
STRIP_CMD := "$(STRIP)"
LD_CMD := "$(LD)"
DEPLOY_CMD := "$(PS5_DEPLOY)"

ifeq ($(HOST_IS_WINDOWS),1)
CURDIR_POSIX := $(shell pwd)
PS5_PAYLOAD_SDK_POSIX := $(shell cygpath -u "$(PS5_PAYLOAD_SDK)" 2>/dev/null || printf '%s' "$(PS5_PAYLOAD_SDK)")
LLVM_CONFIG_POSIX := $(shell cygpath -u "$(LLVM_CONFIG)" 2>/dev/null || printf '%s' "$(LLVM_CONFIG)")
LLVM_BINDIR_POSIX := $(shell cygpath -u "$(LLVM_BINDIR)" 2>/dev/null || printf '%s' "$(LLVM_BINDIR)")
RUN_ENV := cd "$(CURDIR_POSIX)" && export LLVM_CONFIG="$(LLVM_CONFIG_POSIX)" && export LLVM_BINDIR="$(LLVM_BINDIR_POSIX)"
STRIP_CMD := "$(LLVM_BINDIR_POSIX)/llvm-strip"
WINDOWS_LINK_PREFIX := --gc-sections --sysroot="$(PS5_PAYLOAD_SDK_POSIX)"
WINDOWS_LINK_PREFIX += -L"$(PS5_PAYLOAD_SDK_POSIX)/target/lib"
WINDOWS_LINK_PREFIX += -L"$(PS5_PAYLOAD_SDK_POSIX)/target/user/homebrew/lib"
WINDOWS_LINK_PREFIX += -l:crt1.o -l:crti.o -l:crtbegin.o -lc
WINDOWS_BASE_SUFFIX := -lkernel_web -lSceLibcInternal
WINDOWS_BASE_SUFFIX += -lc_stub_weak -lkernel_stub_weak
WINDOWS_BASE_SUFFIX += -l:crtend.o -l:crtn.o
WINDOWS_WEB_SUFFIX := -lkernel_web -lSceLibcInternal -lSceNet
WINDOWS_WEB_SUFFIX += -lc_stub_weak -lkernel_stub_weak
WINDOWS_WEB_SUFFIX += -l:crtend.o -l:crtn.o
WINDOWS_APPINST_SUFFIX := -lSceAppInstUtil $(WINDOWS_BASE_SUFFIX)
define run
bash -lc '$(RUN_ENV) && $(1)'
endef
else
define run
$(1)
endef
endif

all: bfpilot debug full launcher-installer launcher-installer-safe hello-boot hello-http hello-notify installer-enter-probe installer-linkonly-appinst installer-runtime-resolve-appinst

bfpilot: $(BFPILOT_BIN)

debug: $(DEBUG_BIN)

full: $(FULL_BIN)

legacy-full: full

launcher-installer: $(LAUNCHER_INSTALLER_BIN)

launcher-installer-safe: $(SAFE_LAUNCHER_INSTALLER_BIN)

hello-boot: $(HELLO_BOOT_BIN)

hello-http: $(HELLO_HTTP_BIN)

hello-notify: $(HELLO_NOTIFY_BIN)

installer-enter-probe: $(INSTALLER_ENTER_PROBE_BIN)

installer-linkonly-appinst: $(INSTALLER_LINKONLY_APPINST_BIN)

installer-runtime-resolve-appinst: $(INSTALLER_RUNTIME_RESOLVE_APPINST_BIN)

gen/assets:
	$(call run,mkdir -p $@)

gen/assets/%.c: assets/% | gen/assets
	$(call run,$(PYTHON) gen-asset-module.py --path $* $< > $@)

build/bfpilot/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(BFPILOT_CFLAGS) -c $< -o $@)

build/debug/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(DEBUG_CFLAGS) -c $< -o $@)

build/legacy-full/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(FULL_CFLAGS) -c $< -o $@)

build/launcher-installer/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(LAUNCHER_INSTALLER_CFLAGS) -c $< -o $@)

build/launcher-installer-safe/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(SAFE_LAUNCHER_INSTALLER_CFLAGS) -c $< -o $@)

build/hello-boot/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(HELLO_BOOT_CFLAGS) -c $< -o $@)

build/hello-http/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(HELLO_HTTP_CFLAGS) -c $< -o $@)

build/hello-notify/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(HELLO_NOTIFY_CFLAGS) -c $< -o $@)

build/installer-enter-probe/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(INSTALLER_ENTER_PROBE_CFLAGS) -c $< -o $@)

build/installer-linkonly-appinst/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(INSTALLER_LINKONLY_APPINST_CFLAGS) -c $< -o $@)

build/installer-runtime-resolve-appinst/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(INSTALLER_RUNTIME_RESOLVE_APPINST_CFLAGS) -c $< -o $@)

ifeq ($(HOST_IS_WINDOWS),1)
$(BFPILOT_BIN): $(BFPILOT_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(BFPILOT_OBJS) $(WINDOWS_WEB_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)
	$(call run,$(PYTHON) scripts/scrub_main_payload.py $@)

$(DEBUG_BIN): $(DEBUG_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(DEBUG_OBJS) $(WINDOWS_WEB_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)
	$(call run,$(PYTHON) scripts/scrub_main_payload.py $@)

$(FULL_BIN): $(LEGACY_FULL_OBJS) $(APP_ASSETS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(LEGACY_FULL_OBJS) $(WINDOWS_WEB_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(LAUNCHER_INSTALLER_BIN): $(LAUNCHER_INSTALLER_OBJS) $(APP_ASSETS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(LAUNCHER_INSTALLER_OBJS) $(WINDOWS_APPINST_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(SAFE_LAUNCHER_INSTALLER_BIN): $(SAFE_LAUNCHER_INSTALLER_OBJS) $(APP_ASSETS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(SAFE_LAUNCHER_INSTALLER_OBJS) $(WINDOWS_BASE_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_BOOT_BIN): $(HELLO_BOOT_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(HELLO_BOOT_OBJS) $(WINDOWS_BASE_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_HTTP_BIN): $(HELLO_HTTP_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(HELLO_HTTP_OBJS) $(WINDOWS_WEB_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_NOTIFY_BIN): $(HELLO_NOTIFY_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(HELLO_NOTIFY_OBJS) $(WINDOWS_BASE_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_ENTER_PROBE_BIN): $(INSTALLER_ENTER_PROBE_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(INSTALLER_ENTER_PROBE_OBJS) $(WINDOWS_BASE_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_LINKONLY_APPINST_BIN): $(INSTALLER_LINKONLY_APPINST_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(INSTALLER_LINKONLY_APPINST_OBJS) $(WINDOWS_APPINST_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_RUNTIME_RESOLVE_APPINST_BIN): $(INSTALLER_RUNTIME_RESOLVE_APPINST_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(INSTALLER_RUNTIME_RESOLVE_APPINST_OBJS) $(WINDOWS_BASE_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)
else
$(BFPILOT_BIN): $(BFPILOT_OBJS)
	$(call run,$(CC_CMD) $(BFPILOT_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(BFPILOT_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)
	$(call run,$(PYTHON) scripts/scrub_main_payload.py $@)

$(DEBUG_BIN): $(DEBUG_OBJS)
	$(call run,$(CC_CMD) $(DEBUG_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(DEBUG_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)
	$(call run,$(PYTHON) scripts/scrub_main_payload.py $@)

$(FULL_BIN): $(LEGACY_FULL_OBJS) $(APP_ASSETS)
	$(call run,$(CC_CMD) $(FULL_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(LEGACY_FULL_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(LAUNCHER_INSTALLER_BIN): $(LAUNCHER_INSTALLER_OBJS) $(APP_ASSETS)
	$(call run,$(CC_CMD) $(LAUNCHER_INSTALLER_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(LAUNCHER_INSTALLER_OBJS) $(APPINST_LDLIBS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(SAFE_LAUNCHER_INSTALLER_BIN): $(SAFE_LAUNCHER_INSTALLER_OBJS) $(APP_ASSETS)
	$(call run,$(CC_CMD) $(SAFE_LAUNCHER_INSTALLER_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(SAFE_LAUNCHER_INSTALLER_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_BOOT_BIN): $(HELLO_BOOT_OBJS)
	$(call run,$(CC_CMD) $(HELLO_BOOT_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(HELLO_BOOT_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_HTTP_BIN): $(HELLO_HTTP_OBJS)
	$(call run,$(CC_CMD) $(HELLO_HTTP_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(HELLO_HTTP_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(HELLO_NOTIFY_BIN): $(HELLO_NOTIFY_OBJS)
	$(call run,$(CC_CMD) $(HELLO_NOTIFY_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(HELLO_NOTIFY_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_ENTER_PROBE_BIN): $(INSTALLER_ENTER_PROBE_OBJS)
	$(call run,$(CC_CMD) $(INSTALLER_ENTER_PROBE_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(INSTALLER_ENTER_PROBE_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_LINKONLY_APPINST_BIN): $(INSTALLER_LINKONLY_APPINST_OBJS)
	$(call run,$(CC_CMD) $(INSTALLER_LINKONLY_APPINST_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(INSTALLER_LINKONLY_APPINST_OBJS) $(APPINST_LDLIBS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(INSTALLER_RUNTIME_RESOLVE_APPINST_BIN): $(INSTALLER_RUNTIME_RESOLVE_APPINST_OBJS)
	$(call run,$(CC_CMD) $(INSTALLER_RUNTIME_RESOLVE_APPINST_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(INSTALLER_RUNTIME_RESOLVE_APPINST_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)
endif

inspect-imports: all
	$(call run,bash scripts/inspect_imports.sh $(BFPILOT_BIN) $(DEBUG_BIN) $(FULL_BIN) $(LAUNCHER_INSTALLER_BIN) $(SAFE_LAUNCHER_INSTALLER_BIN) $(HELLO_BOOT_BIN) $(HELLO_HTTP_BIN) $(HELLO_NOTIFY_BIN) $(INSTALLER_ENTER_PROBE_BIN) $(INSTALLER_LINKONLY_APPINST_BIN) $(INSTALLER_RUNTIME_RESOLVE_APPINST_BIN))

deploy-bfpilot: bfpilot
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(BFPILOT_BIN))

deploy-debug: debug
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(DEBUG_BIN))

deploy-full: full
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(FULL_BIN))

deploy-launcher-installer: launcher-installer
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(LAUNCHER_INSTALLER_BIN))

clean:
	$(call run,rm -rf $(BFPILOT_BIN) $(DEBUG_BIN) $(FULL_BIN) bfpilot-full-legacy.elf $(LAUNCHER_INSTALLER_BIN) $(SAFE_LAUNCHER_INSTALLER_BIN) bfpilot-core.elf build gen $(HELLO_BOOT_BIN) $(HELLO_HTTP_BIN) $(HELLO_NOTIFY_BIN) $(INSTALLER_ENTER_PROBE_BIN) $(INSTALLER_LINKONLY_APPINST_BIN) $(INSTALLER_RUNTIME_RESOLVE_APPINST_BIN) tests/hello_appinst.elf)

.SECONDARY: $(GEN_SRCS)
.PHONY: all bfpilot debug full legacy-full launcher-installer launcher-installer-safe hello-boot hello-http hello-notify installer-enter-probe installer-linkonly-appinst installer-runtime-resolve-appinst inspect-imports clean deploy-bfpilot deploy-debug deploy-full deploy-launcher-installer
