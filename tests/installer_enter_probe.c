/*
 * BFpilot test payload - installer entry marker with safe imports only.
 */

#include "boot_marker.h"
#include "notify.h"
#include "probe_common.h"

#define INSTALLER_ENTER_MARKER "/data/BFpilot/installer_enter_probe.txt"


int
main(void) {
  int marker_rc = probe_write_marker(INSTALLER_ENTER_MARKER,
                                     "tests/installer_enter_probe");
  bfpilot_boot_marker("tests/installer_enter_probe", BFPILOT_BUILD_MODE);
  printf("BFpilot installer_enter_probe marker_rc=%d\n", marker_rc);
  bfpilot_notify("BFpilot installer enter probe",
                 marker_rc == 0 ? "entered main" : "marker write failed");
  return marker_rc == 0 ? 0 : 1;
}
