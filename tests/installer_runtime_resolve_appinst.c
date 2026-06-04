/*
 * BFpilot test payload - runtime AppInstUtil load/resolve probe.
 */

#include <stdint.h>
#include <stdio.h>

#include <ps5/kernel.h>

#include "boot_marker.h"
#include "notify.h"
#include "probe_common.h"
#include "version.h"

#define RUNTIME_RESOLVE_MARKER "/data/BFpilot/runtime_resolve_entered.txt"
#define RUNTIME_RESOLVE_LOG "/data/BFpilot/runtime_resolve_appinst.log"
#define APPINST_MODULE "libSceAppInstUtil.sprx"
#define NID_INSTALL_TITLE_DIR "Wudg3Xe3heE"


int
main(void) {
  int marker_rc = probe_write_marker(RUNTIME_RESOLVE_MARKER,
                                     "tests/installer_runtime_resolve_appinst");
  bfpilot_boot_marker("tests/installer_runtime_resolve_appinst",
                      BFPILOT_BUILD_MODE);

  probe_log(RUNTIME_RESOLVE_LOG,
            "entered main marker_rc=%d tag=%s build=%s",
            marker_rc, VERSION_TAG, BUILD_VERSION);

  uint32_t handle = 0;
  probe_log(RUNTIME_RESOLVE_LOG, "about to kernel_dynlib_handle %s",
            APPINST_MODULE);
  int handle_rc = kernel_dynlib_handle(-1, APPINST_MODULE, &handle);
  probe_log(RUNTIME_RESOLVE_LOG,
            "kernel_dynlib_handle %s rc=0x%08x handle=0x%08x",
            APPINST_MODULE, handle_rc, handle);

  if(handle_rc == 0) {
    probe_log(RUNTIME_RESOLVE_LOG,
              "about to kernel_dynlib_dlsym sceAppInstUtilInitialize");
    intptr_t init_addr = kernel_dynlib_dlsym(
        -1, handle, "sceAppInstUtilInitialize");
    probe_log(RUNTIME_RESOLVE_LOG,
              "kernel_dynlib_dlsym sceAppInstUtilInitialize addr=%p",
              (void *)init_addr);

    probe_log(RUNTIME_RESOLVE_LOG,
              "about to kernel_dynlib_dlsym sceAppInstUtilAppInstallAll");
    intptr_t install_all_addr = kernel_dynlib_dlsym(
        -1, handle, "sceAppInstUtilAppInstallAll");
    probe_log(RUNTIME_RESOLVE_LOG,
              "kernel_dynlib_dlsym sceAppInstUtilAppInstallAll addr=%p",
              (void *)install_all_addr);

    probe_log(RUNTIME_RESOLVE_LOG,
              "about to kernel_dynlib_resolve AppInstallTitleDir nid=%s",
              NID_INSTALL_TITLE_DIR);
    intptr_t title_dir_addr = kernel_dynlib_resolve(
        -1, handle, NID_INSTALL_TITLE_DIR);
    probe_log(RUNTIME_RESOLVE_LOG,
              "kernel_dynlib_resolve AppInstallTitleDir nid=%s addr=%p",
              NID_INSTALL_TITLE_DIR, (void *)title_dir_addr);
  } else {
    probe_log(RUNTIME_RESOLVE_LOG,
              "AppInstUtil not already loaded; skipping dlopen because prior "
              "test stopped before the dlopen result log");
  }

  bfpilot_notify("BFpilot runtime AppInst",
                 marker_rc == 0 ? "runtime resolve probe complete"
                                : "marker write failed");
  return marker_rc == 0 ? 0 : 1;
}
