/*
 * BFpilot probe - exact direct-import composition used by ps5 websrv.
 */

#include <stdint.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "boot_marker.h"
#include "notify.h"
#include "probe_common.h"

#define MARKER "/data/BFpilot/websrv_pattern_entered.txt"
#define LOG_PATH "/data/BFpilot/websrv_pattern.log"
#define APPINST_AUTHID UINT64_C(0x4801000000000013)

int sceUserServiceInitialize(void *);
void sceUserServiceTerminate(void);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceAppInstUtilInitialize(void);


int
main(void) {
  int marker_rc = probe_write_marker(MARKER, "tests/installer_websrv_pattern");
  bfpilot_boot_marker("tests/installer_websrv_pattern",
                      "installer-websrv-pattern");
  probe_log(LOG_PATH, "entered main marker_rc=%d", marker_rc);

  int user_rc = sceUserServiceInitialize(NULL);
  probe_log(LOG_PATH, "sceUserServiceInitialize rc=0x%08x", user_rc);

  pid_t pid = getpid();
  uint64_t original_authid = kernel_get_ucred_authid(pid);
  int authid_rc = kernel_set_ucred_authid(pid, APPINST_AUTHID);
  probe_log(LOG_PATH, "set authid rc=0x%08x original=0x%016llx current=0x%016llx",
            authid_rc, (unsigned long long)original_authid,
            (unsigned long long)kernel_get_ucred_authid(pid));

  int appinst_rc = sceAppInstUtilInitialize();
  probe_log(LOG_PATH, "sceAppInstUtilInitialize rc=0x%08x", appinst_rc);

  if(original_authid != 0) {
    int restore_rc = kernel_set_ucred_authid(pid, original_authid);
    probe_log(LOG_PATH, "restore authid rc=0x%08x current=0x%016llx",
              restore_rc,
              (unsigned long long)kernel_get_ucred_authid(pid));
  }
  sceUserServiceTerminate();
  bfpilot_notify("BFpilot websrv-pattern probe", "probe complete");
  return appinst_rc == 0 ? 0 : 1;
}
