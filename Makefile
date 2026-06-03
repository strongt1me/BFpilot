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

VERSION_TAG := bfpilot-v0.2.1
BUILD_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

LLVM_BINDIR ?= $(shell dirname "$$(command -v clang 2>/dev/null || command -v clang.exe 2>/dev/null || command -v llvm-strip 2>/dev/null || command -v llvm-strip.exe 2>/dev/null || echo clang)" 2>/dev/null || echo .)
LLVM_CONFIG ?= $(CURDIR)/build-tools/llvm-config
export LLVM_BINDIR
export LLVM_CONFIG

CORE_BIN := bfpilot-core.elf
FULL_BIN := bfpilot-full.elf

COMMON_SRCS := src/lite_main.c
COMMON_SRCS += src/diag.c
COMMON_SRCS += src/websrv_lite.c
COMMON_SRCS += src/asset.c
COMMON_SRCS += src/fs.c
COMMON_SRCS += src/mime.c
COMMON_SRCS += src/notify.c
COMMON_SRCS += src/transfer.c

FULL_SRCS := $(COMMON_SRCS)
FULL_SRCS += src/app_installer.c
FULL_SRCS += src/sce_resolve.c

ASSETS := $(wildcard assets/*)
GEN_SRCS := $(patsubst assets/%,gen/assets/%,$(ASSETS:=.c))
APP_ASSETS := assets-app/param.json assets-app/icon0.png
CORE_OBJS := $(patsubst %.c,build/core/%.o,$(COMMON_SRCS) $(GEN_SRCS))
FULL_OBJS := $(patsubst %.c,build/full/%.o,$(FULL_SRCS) $(GEN_SRCS))

COMMON_CFLAGS := -Os -Wall -Werror -Isrc
COMMON_CFLAGS += -ffunction-sections -fdata-sections -flto
COMMON_CFLAGS += -DVERSION_TAG=\"$(VERSION_TAG)\"
COMMON_CFLAGS += -DBUILD_VERSION=\"$(BUILD_VERSION)\"
COMMON_CFLAGS += -DBFPILOT_SDK_PATH=\"$(PS5_PAYLOAD_SDK)\"

CORE_CFLAGS := $(COMMON_CFLAGS)
CORE_CFLAGS += -DBFPILOT_ENABLE_LAUNCHER=0
CORE_CFLAGS += -DBFPILOT_DISABLE_LAUNCHER=1
CORE_CFLAGS += -DBFPILOT_BUILD_MODE=\"core\"

FULL_CFLAGS := $(COMMON_CFLAGS)
FULL_CFLAGS += -DBFPILOT_ENABLE_LAUNCHER=1
FULL_CFLAGS += -DBFPILOT_BUILD_MODE=\"full\"

COMMON_LDFLAGS := -Wl,--gc-sections -flto

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
WINDOWS_LINK_SUFFIX := -lkernel_web -lSceLibcInternal -lSceNet
WINDOWS_LINK_SUFFIX += -lc_stub_weak -lkernel_stub_weak
WINDOWS_LINK_SUFFIX += -l:crtend.o -l:crtn.o
define run
bash -lc '$(RUN_ENV) && $(1)'
endef
else
define run
$(1)
endef
endif

all: core full

core: $(CORE_BIN)

full: $(FULL_BIN)

gen/assets:
	$(call run,mkdir -p $@)

gen/assets/%.c: assets/% | gen/assets
	$(call run,$(PYTHON) gen-asset-module.py --path $* $< > $@)

build/core/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(CORE_CFLAGS) -c $< -o $@)

build/full/%.o: %.c Makefile
	$(call run,mkdir -p $(dir $@))
	$(call run,$(CC_CMD) $(FULL_CFLAGS) -c $< -o $@)

ifeq ($(HOST_IS_WINDOWS),1)
$(CORE_BIN): $(CORE_OBJS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(CORE_OBJS) $(WINDOWS_LINK_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(FULL_BIN): $(FULL_OBJS) $(APP_ASSETS)
	$(call run,$(LD_CMD) -o $@ $(WINDOWS_LINK_PREFIX) $(FULL_OBJS) $(WINDOWS_LINK_SUFFIX))
	$(call run,$(STRIP_CMD) --strip-all $@)
else
$(CORE_BIN): $(CORE_OBJS)
	$(call run,$(CC_CMD) $(CORE_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(CORE_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)

$(FULL_BIN): $(FULL_OBJS) $(APP_ASSETS)
	$(call run,$(CC_CMD) $(FULL_CFLAGS) $(COMMON_LDFLAGS) -o $@ $(FULL_OBJS))
	$(call run,$(STRIP_CMD) --strip-all $@)
endif

deploy-core: core
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(CORE_BIN))

deploy-full: full
	$(call run,$(DEPLOY_CMD) -h $(PS5_HOST) -p $(PS5_PORT) $(FULL_BIN))

clean:
	$(call run,rm -rf $(CORE_BIN) $(FULL_BIN) build gen)

.SECONDARY: $(GEN_SRCS)
.PHONY: all core full clean deploy-core deploy-full
