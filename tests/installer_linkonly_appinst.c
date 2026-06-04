/*
 * BFpilot test payload - direct AppInst import/link probe.
 *
 * This payload intentionally links AppInstUtil but never calls it. If the
 * marker file is not written, the loader rejected the ELF before main().
 */

#include "boot_marker.h"
#include "notify.h"
#include "probe_common.h"

#define LINKONLY_APPINST_MARKER "/data/BFpilot/linkonly_appinst_entered.txt"

int sceAppInstUtilInitialize(void);

__attribute__((used)) static void * volatile g_appinst_import_probe =
    (void *)&sceAppInstUtilInitialize;


int
main(void) {
  int marker_rc = probe_write_marker(LINKONLY_APPINST_MARKER,
                                     "tests/installer_linkonly_appinst");
  bfpilot_boot_marker("tests/installer_linkonly_appinst", BFPILOT_BUILD_MODE);
  printf("BFpilot installer_linkonly_appinst marker_rc=%d import_symbol=%p\n",
         marker_rc, g_appinst_import_probe);
  bfpilot_notify("BFpilot link-only AppInst",
                 marker_rc == 0 ? "entered main" : "marker write failed");
  return marker_rc == 0 ? 0 : 1;
}
