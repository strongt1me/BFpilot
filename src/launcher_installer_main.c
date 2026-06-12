/*
 * BFpilot - isolated optional launcher tile installer.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "boot_marker.h"
#include "notify.h"
#include "sce_resolve.h"
#include "version.h"

#define BFPILOT_APP_TITLE_ID "BFPL00001"
#define BFPILOT_APP_ROOT "/user/app"
#define BFPILOT_APP_PARENT BFPILOT_APP_ROOT "/"
#define BFPILOT_DATA_ROOT "/data"
#define BFPILOT_DATA_DIR "/data/BFpilot"
#define BFPILOT_LAUNCHER_LOG "/data/BFpilot/launcher-installer.log"
#define BFPILOT_APPINST_MODULE "libSceAppInstUtil.sprx"
#define BFPILOT_NID_INSTALL_TITLE_DIR "Wudg3Xe3heE"
#define BFPILOT_DIAG_SKIPPED (-2147483000)

#define INCASSET(name, file)                                                   \
  __asm__(".section .rodata\n"                                                 \
          ".global " #name "\n"                                                \
          ".global " #name "_end\n"                                            \
          ".global " #name "_size\n"                                           \
          ".align 16\n" #name ":\n"                                            \
          ".incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n"            \
          ".quad " #name "_end - " #name "\n"                                  \
          ".previous\n");                                                      \
  extern const uint8_t name[];                                                 \
  extern const size_t name##_size

INCASSET(bfpilot_param_json, "assets-app/param.json");
INCASSET(bfpilot_icon0_png, "assets-app/icon0.png");

typedef int (*appinst_init_fn)(void);
typedef int (*app_install_title_dir_fn)(const char *, const char *, void *);
typedef int (*app_install_all_fn)(void *);


static int
mkdir_if_needed(const char *path) {
  if(mkdir(path, 0755) == 0) return 0;
  return errno == EEXIST ? 0 : -errno;
}


static void
ensure_log_dir(void) {
  (void)mkdir_if_needed(BFPILOT_DATA_ROOT);
  (void)mkdir_if_needed(BFPILOT_DATA_DIR);
}


static void
installer_log(const char *fmt, ...) {
  char body[1024];
  char line[1400];
  va_list ap;

  va_start(ap, fmt);
  int body_n = vsnprintf(body, sizeof(body), fmt ? fmt : "", ap);
  va_end(ap);
  if(body_n < 0) {
    snprintf(body, sizeof(body), "log format failed");
  } else if((size_t)body_n >= sizeof(body)) {
    body[sizeof(body) - 1] = 0;
  }

  int n = snprintf(line, sizeof(line), "%ld pid=%ld %s\n",
                   (long)time(NULL), (long)getpid(), body);
  if(n < 0) return;
  if((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;

  fputs(line, stdout);
  fflush(stdout);

  ensure_log_dir();
  int fd = open(BFPILOT_LAUNCHER_LOG,
                O_WRONLY | O_CREAT | O_APPEND, 0600);
  if(fd >= 0) {
    (void)write(fd, line, (size_t)n);
    close(fd);
  }
}


static int
write_file(const char *path, const uint8_t *data, size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -errno;

  size_t done = 0;
  while(done < size) {
    ssize_t wr = write(fd, data + done, size - done);
    if(wr < 0) {
      int err = errno;
      close(fd);
      return -err;
    }
    done += (size_t)wr;
  }

  if(close(fd) != 0) return -errno;
  return 0;
}


int
main(void) {
  bfpilot_boot_marker("bfpilot-launcher-installer", BFPILOT_BUILD_MODE);

  char app_dir[256];
  char sce_sys_dir[256];
  char param_path[256];
  char icon_path[256];
  uint32_t appinst_handle = 0;
  appinst_init_fn appinst_init = NULL;
  app_install_title_dir_fn install_title_dir = NULL;
  app_install_all_fn install_all = NULL;
  int appinst_handle_rc;
  int title_dir_rc = BFPILOT_DIAG_SKIPPED;
  int install_all_rc = BFPILOT_DIAG_SKIPPED;
  int appinst_init_rc = BFPILOT_DIAG_SKIPPED;
  int final_rc = -1;
  const char *final_state = "unsupported";

  installer_log("entered main tag=%s build=%s", VERSION_TAG, BUILD_VERSION);
  installer_log("launcher metadata title_id=%s deeplink=http://127.0.0.1:5905/ "
                "param_bytes=%lu icon_bytes=%lu icon_png_magic=%s",
                BFPILOT_APP_TITLE_ID, (unsigned long)bfpilot_param_json_size,
                (unsigned long)bfpilot_icon0_png_size,
                bfpilot_icon0_png_size >= 8 &&
                bfpilot_icon0_png[0] == 0x89 && bfpilot_icon0_png[1] == 'P' &&
                bfpilot_icon0_png[2] == 'N' && bfpilot_icon0_png[3] == 'G'
                    ? "yes" : "no");
  installer_log("about to kernel_dynlib_handle %s", BFPILOT_APPINST_MODULE);
  appinst_handle_rc = kernel_dynlib_handle(-1, BFPILOT_APPINST_MODULE,
                                           &appinst_handle);
  installer_log("kernel_dynlib_handle %s rc=0x%08x handle=0x%08x",
                BFPILOT_APPINST_MODULE, appinst_handle_rc, appinst_handle);

  if(appinst_handle_rc == 0) {
    installer_log("about to kernel_dynlib_dlsym sceAppInstUtilInitialize");
    appinst_init = (appinst_init_fn)kernel_dynlib_dlsym(
        -1, appinst_handle, "sceAppInstUtilInitialize");
    installer_log("kernel_dynlib_dlsym sceAppInstUtilInitialize addr=%p",
                  (void *)appinst_init);

    installer_log("about to kernel_dynlib_dlsym sceAppInstUtilAppInstallAll");
    install_all = (app_install_all_fn)kernel_dynlib_dlsym(
        -1, appinst_handle, "sceAppInstUtilAppInstallAll");
    installer_log("kernel_dynlib_dlsym sceAppInstUtilAppInstallAll addr=%p",
                  (void *)install_all);

    installer_log("about to kernel_dynlib_resolve AppInstallTitleDir nid=%s",
                  BFPILOT_NID_INSTALL_TITLE_DIR);
    install_title_dir = (app_install_title_dir_fn)kernel_dynlib_resolve(
        -1, appinst_handle, BFPILOT_NID_INSTALL_TITLE_DIR);
    installer_log("kernel_dynlib_resolve AppInstallTitleDir addr=%p",
                  (void *)install_title_dir);

    if(!install_title_dir) {
      installer_log("about to kernel_dynlib_dlsym "
                    "sceAppInstUtilAppInstallTitleDir");
      install_title_dir = (app_install_title_dir_fn)kernel_dynlib_dlsym(
          -1, appinst_handle, "sceAppInstUtilAppInstallTitleDir");
      installer_log("kernel_dynlib_dlsym sceAppInstUtilAppInstallTitleDir "
                    "addr=%p", (void *)install_title_dir);
    }
  } else {
    installer_log("AppInstUtil unavailable without direct import; skipping "
                  "launcher install safely");
  }

  if(appinst_init) {
    appinst_init_rc = appinst_init();
  }
  installer_log("AppInst init return code=0x%08x", appinst_init_rc);

  int appinst_symbols_available =
      appinst_handle_rc == 0 && appinst_init && (install_title_dir || install_all);
  int can_stage_launcher = appinst_symbols_available && appinst_init_rc == 0;

  snprintf(app_dir, sizeof(app_dir), BFPILOT_APP_ROOT "/%s",
           BFPILOT_APP_TITLE_ID);
  snprintf(sce_sys_dir, sizeof(sce_sys_dir), "%s/sce_sys", app_dir);
  snprintf(param_path, sizeof(param_path), "%s/param.json", sce_sys_dir);
  snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", sce_sys_dir);

  int mkdir_app_rc = BFPILOT_DIAG_SKIPPED;
  int mkdir_sce_sys_rc = BFPILOT_DIAG_SKIPPED;
  int write_param_rc = BFPILOT_DIAG_SKIPPED;
  int write_icon_rc = BFPILOT_DIAG_SKIPPED;

  if(can_stage_launcher) {
    errno = 0;
    mkdir_app_rc = mkdir_if_needed(app_dir);
    installer_log("%s mkdir return code=%d errno=%d",
                  app_dir, mkdir_app_rc, errno);
    errno = 0;
    mkdir_sce_sys_rc = mkdir_if_needed(sce_sys_dir);
    installer_log("%s mkdir return code=%d errno=%d",
                  sce_sys_dir, mkdir_sce_sys_rc, errno);
    errno = 0;
    write_param_rc = write_file(param_path, bfpilot_param_json,
                                bfpilot_param_json_size);
    installer_log("%s write return code=%d errno=%d",
                  param_path, write_param_rc, errno);
    errno = 0;
    write_icon_rc = write_file(icon_path, bfpilot_icon0_png,
                               bfpilot_icon0_png_size);
    installer_log("%s write return code=%d errno=%d",
                  icon_path, write_icon_rc, errno);
  } else {
    const char *reason = !appinst_symbols_available ?
        "AppInstUtil not safely available" : "AppInst init failed";
    installer_log("launcher staging skipped: %s", reason);
    installer_log("%s mkdir return code=skipped", app_dir);
    installer_log("%s mkdir return code=skipped", sce_sys_dir);
    installer_log("%s write return code=skipped", param_path);
    installer_log("%s write return code=skipped", icon_path);
  }

  installer_log("AppInstallTitleDir resolved=%s nid=%s",
                install_title_dir ? "yes" : "no",
                BFPILOT_NID_INSTALL_TITLE_DIR);

  if(can_stage_launcher && mkdir_app_rc == 0 && mkdir_sce_sys_rc == 0 &&
     write_param_rc == 0 && write_icon_rc == 0) {
    if(install_title_dir) {
      title_dir_rc = install_title_dir(BFPILOT_APP_TITLE_ID,
                                       BFPILOT_APP_PARENT, NULL);
      installer_log("AppInstallTitleDir return code=0x%08x", title_dir_rc);
    } else {
      installer_log("AppInstallTitleDir return code=skipped");
    }

    if(title_dir_rc != 0) {
      if(install_all) {
        install_all_rc = install_all(NULL);
        installer_log("AppInstallAll fallback return code=0x%08x",
                      install_all_rc);
      } else {
        installer_log("AppInstallAll fallback return code=skipped");
      }
    } else {
      installer_log("AppInstallAll fallback return code=skipped");
    }
  } else {
    installer_log("AppInstallTitleDir return code=skipped");
    installer_log("AppInstallAll fallback return code=skipped");
  }

  if(title_dir_rc == 0 || install_all_rc == 0) {
    final_rc = 0;
    final_state = "installed";
  } else if(appinst_symbols_available) {
    final_state = "failed_nonfatal";
  }

  installer_log("final result=%s final_rc=%d appinst_init_rc=0x%08x "
                "mkdir_app_rc=%d mkdir_sce_sys_rc=%d write_param_rc=%d "
                "write_icon_rc=%d title_dir_rc=0x%08x install_all_rc=0x%08x",
                final_state, final_rc,
                appinst_init_rc, mkdir_app_rc, mkdir_sce_sys_rc,
                write_param_rc, write_icon_rc, title_dir_rc, install_all_rc);

  if(final_rc == 0) {
    bfpilot_notify("BFpilot launcher installer", "installed or refreshed");
    return 0;
  }

  if(appinst_symbols_available) {
    bfpilot_notify("BFpilot launcher installer",
                   "failed; file manager unaffected");
    return 1;
  }

  bfpilot_notify("BFpilot launcher installer",
                 "unsupported on this loader; use web UI");
  return 2;
}
