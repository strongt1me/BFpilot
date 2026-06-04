/*
 * BFpilot test payload - notification probe.
 */

#include <stdio.h>

#include "boot_marker.h"
#include "notify.h"


int
main(void) {
  bfpilot_boot_marker("tests/hello_notify", BFPILOT_BUILD_MODE);
  int rc = bfpilot_notify_send("BFpilot hello_notify",
                               "notification probe");
  printf("BFpilot hello_notify notification rc=0x%08x\n", rc);
  return rc == 0 ? 0 : 1;
}
