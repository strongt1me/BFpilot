/*
 * BFpilot probe - direct Sysmodule import, then load and resolve AppInstUtil.
 */

#include <stdint.h>

#include <ps5/kernel.h>

#include "boot_marker.h"
#include "notify.h"
#include "probe_common.h"

#define MARKER "/data/BFpilot/sysmodule_appinst_entered.txt"
#define LOG_PATH "/data/BFpilot/sysmodule_appinst.log"
#define APPINST_MODULE "libSceAppInstUtil.sprx"
#define APPINST_INTERNAL_ID 0x80000014U

int sceSysmoduleLoadModuleInternal(unsigned int);


int
main(void) {
  int marker_rc = probe_write_marker(MARKER, "tests/installer_sysmodule_appinst");
  bfpilot_boot_marker("tests/installer_sysmodule_appinst",
                      "installer-sysmodule-appinst");
  probe_log(LOG_PATH, "entered main marker_rc=%d", marker_rc);
  probe_log(LOG_PATH, "about to sceSysmoduleLoadModuleInternal id=0x%08x",
            APPINST_INTERNAL_ID);
  int load_rc = sceSysmoduleLoadModuleInternal(APPINST_INTERNAL_ID);
  probe_log(LOG_PATH, "sceSysmoduleLoadModuleInternal rc=0x%08x", load_rc);

  uint32_t handle = 0;
  int handle_rc = kernel_dynlib_handle(-1, APPINST_MODULE, &handle);
  probe_log(LOG_PATH, "kernel_dynlib_handle %s rc=0x%08x handle=0x%08x",
            APPINST_MODULE, handle_rc, handle);
  if(handle_rc == 0) {
    intptr_t init = kernel_dynlib_dlsym(
        -1, handle, "sceAppInstUtilInitialize");
    intptr_t install_all = kernel_dynlib_dlsym(
        -1, handle, "sceAppInstUtilAppInstallAll");
    probe_log(LOG_PATH, "symbols initialize=%p install_all=%p",
              (void *)init, (void *)install_all);
  }
  bfpilot_notify("BFpilot Sysmodule AppInst", "probe complete");
  return handle_rc == 0 ? 0 : 1;
}
