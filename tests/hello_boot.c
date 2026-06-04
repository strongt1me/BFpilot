/*
 * BFpilot test payload - boot marker only.
 */

#include <unistd.h>

#include "boot_marker.h"
#include "notify.h"


int
main(void) {
  bfpilot_boot_marker("tests/hello_boot", BFPILOT_BUILD_MODE);
  sleep(10);
  bfpilot_notify("BFpilot BOOT still alive", "tests/hello_boot");
  return 0;
}
