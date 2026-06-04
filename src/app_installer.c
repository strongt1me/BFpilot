/*
 * BFpilot - install the PS5 home-screen web launcher tile.
 */

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_installer.h"
#include "diag.h"
#include "notify.h"
#include "sce_resolve.h"
#include "websrv.h"

#define BFPILOT_APP_TITLE_ID "BFPL00001"
#define BFPILOT_INVALID_APP_TITLE_ID "BFPILOT01"
#define BFPILOT_LEGACY_APP_TITLE_ID "BSFM00001"
#define BFPILOT_OLD_LEGACY_APP_TITLE_ID "BS5F00001"
#define BFPILOT_APP_ROOT "/user/app"
#define BFPILOT_APP_PARENT BFPILOT_APP_ROOT "/"
#define BFPILOT_DATA_DIR "/data/BFpilot"
#define BFPILOT_MARKER_PATH BFPILOT_DATA_DIR "/launcher.ok"
#define BFPILOT_INSTALL_MARKER "bfpilot-launcher-v2\n"
#define BFPILOT_APPINST_MODULE "libSceAppInstUtil.sprx"
#define BFPILOT_NID_INSTALL_TITLE_DIR "Wudg3Xe3heE"

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

typedef int (*appinst_initialize_fn)(void);
typedef int (*appinst_uninstall_fn)(const char *);
typedef int (*app_install_title_dir_fn)(const char *, const char *, void *);
typedef int (*appinst_install_all_fn)(void *);

typedef struct appinst_api {
  appinst_initialize_fn       initialize;
  appinst_uninstall_fn        uninstall;
  app_install_title_dir_fn    install_title_dir;
  appinst_install_all_fn      install_all;
} appinst_api_t;

static const uint8_t g_install_marker[] = BFPILOT_INSTALL_MARKER;
static bfpilot_launcher_diag_t g_launcher_diag = {
  .launcher_enabled = 1,
  .appinst_init_rc = -1,
  .install_title_dir_resolved = 0,
  .uninstall_resolved = 0,
  .install_all_resolved = 0,
  .user_app_writable = -1,
  .launcher_install_rc = -1,
};


static void
publish_launcher_diag(void) {
  websrv_set_launcher_diag(&g_launcher_diag);
}


static void
reset_launcher_diag(void) {
  bfpilot_launcher_diag_t diag = {
    .launcher_enabled = 1,
    .appinst_init_rc = -1,
    .install_title_dir_resolved = 0,
    .uninstall_resolved = 0,
    .install_all_resolved = 0,
    .user_app_writable = -1,
    .launcher_install_rc = -1,
  };
  g_launcher_diag = diag;
  publish_launcher_diag();
}


static int
resolve_optional(void **out, const char *symbol, const char *fallback_symbol) {
  int rc = sce_resolve_symbol(BFPILOT_APPINST_MODULE, symbol, out);
  if(rc != 0 && fallback_symbol) {
    rc = sce_resolve_symbol(BFPILOT_APPINST_MODULE, fallback_symbol, out);
  }
  return rc;
}


static void
resolve_appinst(appinst_api_t *api) {
  memset(api, 0, sizeof(*api));

  int rc = resolve_optional((void **)&api->initialize,
                            "sceAppInstUtilInitialize", NULL);
  bfpilot_log("launcher resolve Initialize %s rc=0x%08x",
              api->initialize ? "ok" : "missing", rc);

  rc = resolve_optional((void **)&api->uninstall,
                        "sceAppInstUtilAppUnInstall", NULL);
  g_launcher_diag.uninstall_resolved = api->uninstall != NULL;
  bfpilot_log("launcher resolve AppUnInstall %s rc=0x%08x",
              api->uninstall ? "ok" : "missing", rc);

  rc = resolve_optional((void **)&api->install_title_dir,
                        "sceAppInstUtilAppInstallTitleDir",
                        BFPILOT_NID_INSTALL_TITLE_DIR);
  g_launcher_diag.install_title_dir_resolved = api->install_title_dir != NULL;
  bfpilot_log("launcher resolve AppInstallTitleDir %s rc=0x%08x",
              api->install_title_dir ? "ok" : "missing", rc);

  rc = resolve_optional((void **)&api->install_all,
                        "sceAppInstUtilAppInstallAll", NULL);
  g_launcher_diag.install_all_resolved = api->install_all != NULL;
  bfpilot_log("launcher resolve AppInstallAll %s rc=0x%08x",
              api->install_all ? "ok" : "missing", rc);

  publish_launcher_diag();
}


static int
probe_dir_writable(const char *dir) {
  char probe[256];
  int n = snprintf(probe, sizeof(probe), "%s/.bfpilot_write_probe", dir);
  if(n < 0 || (size_t)n >= sizeof(probe)) return 0;

  int fd = open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(fd < 0) return 0;

  close(fd);
  unlink(probe);
  return 1;
}


static int
install_app(const appinst_api_t *api, const char *title_id, const char *dir) {
  if(api->install_title_dir) {
    int err = api->install_title_dir(title_id, dir, NULL);
    g_launcher_diag.launcher_install_rc = err;
    publish_launcher_diag();
    if(err == 0) return 0;
    bfpilot_log("launcher install AppInstallTitleDir failed rc=0x%08x", err);
  } else {
    bfpilot_log("launcher install AppInstallTitleDir not resolved");
  }

  if(api->install_all) {
    int err = api->install_all(NULL);
    g_launcher_diag.launcher_install_rc = err;
    publish_launcher_diag();
    bfpilot_log("launcher install AppInstallAll rc=0x%08x", err);
    return err;
  }

  g_launcher_diag.launcher_install_rc = -2;
  publish_launcher_diag();
  bfpilot_log("launcher install AppInstallAll not resolved");
  return -2;
}


static int
write_file(const char *path, const uint8_t *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if(!file) return -1;

  size_t written = fwrite(data, 1, size, file);
  int close_rc = fclose(file);

  return (written == size && close_rc == 0) ? 0 : -1;
}


static int
file_differs(const char *path, const uint8_t *expected, size_t expected_size) {
  struct stat st;
  if(stat(path, &st) != 0) return 1;
  if(st.st_size < 0 || (size_t)st.st_size != expected_size) return 1;

  FILE *file = fopen(path, "rb");
  if(!file) return 1;

  uint8_t *actual = malloc(expected_size ? expected_size : 1);
  if(!actual) {
    fclose(file);
    return 1;
  }

  size_t read = fread(actual, 1, expected_size, file);
  fclose(file);

  int differs = read != expected_size || memcmp(actual, expected, expected_size);
  free(actual);

  return differs;
}


static int
mkdir_if_needed(const char *path) {
  if(mkdir(path, 0755) == 0) return 0;
  return errno == EEXIST ? 0 : -1;
}


static int
remove_tree(const char *path) {
  struct stat st;
  if(lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;

  if(S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    if(!dir) return -1;

    struct dirent *ent;
    while((ent = readdir(dir))) {
      if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

      char child[512];
      int n = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
      if(n < 0 || (size_t)n >= sizeof(child)) {
        closedir(dir);
        errno = ENAMETOOLONG;
        return -1;
      }
      if(remove_tree(child) != 0) {
        closedir(dir);
        return -1;
      }
    }
    closedir(dir);
    return rmdir(path);
  }

  return unlink(path);
}


static int
ensure_data_dir(void) {
  if(mkdir_if_needed("/data") != 0) return -1;
  return mkdir_if_needed(BFPILOT_DATA_DIR);
}


int
bfpilot_install_app_if_needed(void) {
  appinst_api_t api;
  char app_dir[256];
  char sce_sys_dir[256];
  char param_path[256];
  char icon_path[256];
  char msg[128];
  struct stat st;

  reset_launcher_diag();
  resolve_appinst(&api);

  g_launcher_diag.user_app_writable = probe_dir_writable(BFPILOT_APP_ROOT);
  publish_launcher_diag();
  bfpilot_log("launcher install %s writable=%d", BFPILOT_APP_ROOT,
              g_launcher_diag.user_app_writable);

  if(!api.initialize) {
    g_launcher_diag.launcher_install_rc = -2;
    publish_launcher_diag();
    bfpilot_log("launcher install sceAppInstUtilInitialize not resolved");
    bfpilot_notify("BFpilot app failed", "AppInst init not resolved");
    return -1;
  }

  if(!api.install_title_dir && !api.install_all) {
    g_launcher_diag.launcher_install_rc = -2;
    publish_launcher_diag();
    bfpilot_log("launcher install no AppInst install function resolved");
    bfpilot_notify("BFpilot app failed", "AppInst install not resolved");
    return -1;
  }

  snprintf(app_dir, sizeof(app_dir), BFPILOT_APP_ROOT "/%s",
           BFPILOT_APP_TITLE_ID);
  snprintf(sce_sys_dir, sizeof(sce_sys_dir), "%s/sce_sys", app_dir);
  snprintf(param_path, sizeof(param_path), "%s/param.json", sce_sys_dir);
  snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", sce_sys_dir);

  int app_exists = stat(app_dir, &st) == 0;
  int assets_changed = !app_exists ||
                       file_differs(param_path, bfpilot_param_json,
                                    bfpilot_param_json_size) ||
                       file_differs(icon_path, bfpilot_icon0_png,
                                    bfpilot_icon0_png_size) ||
                       file_differs(BFPILOT_MARKER_PATH, g_install_marker,
                                     sizeof(g_install_marker) - 1);

  if(app_exists && assets_changed) {
    bfpilot_notify("BFpilot app", "Updating PS5 home-screen launcher");
  } else if(app_exists) {
    bfpilot_notify("BFpilot app", "Refreshing PS5 home-screen launcher");
  } else {
    bfpilot_notify("BFpilot app", "Installing PS5 home-screen launcher");
  }

  int err = api.initialize();
  g_launcher_diag.appinst_init_rc = err;
  publish_launcher_diag();
  if(err) {
    bfpilot_log("launcher install sceAppInstUtilInitialize failed rc=0x%08x",
                err);
    g_launcher_diag.launcher_install_rc = err;
    publish_launcher_diag();
    snprintf(msg, sizeof(msg), "AppInst init failed 0x%08x", err);
    bfpilot_notify("BFpilot app failed", msg);
    return -1;
  }

  if(api.uninstall) {
    int uninstall_err = api.uninstall(BFPILOT_APP_TITLE_ID);
    bfpilot_log("launcher install refresh BFpilot tile rc=0x%08x",
                uninstall_err);
    uninstall_err = api.uninstall(BFPILOT_INVALID_APP_TITLE_ID);
    bfpilot_log("launcher install remove invalid BFpilot tile rc=0x%08x",
                uninstall_err);
    uninstall_err = api.uninstall(BFPILOT_LEGACY_APP_TITLE_ID);
    bfpilot_log("launcher install remove BS5FileManager tile rc=0x%08x",
                uninstall_err);
    uninstall_err = api.uninstall(BFPILOT_OLD_LEGACY_APP_TITLE_ID);
    bfpilot_log("launcher install remove old legacy tile rc=0x%08x",
                uninstall_err);
  } else {
    bfpilot_log("launcher install AppUnInstall not resolved, skipping removes");
  }

  char invalid_dir[256];
  snprintf(invalid_dir, sizeof(invalid_dir), BFPILOT_APP_ROOT "/%s",
           BFPILOT_INVALID_APP_TITLE_ID);
  if(remove_tree(invalid_dir) != 0) {
    bfpilot_log("launcher install warning failed removing %s errno=%d",
                invalid_dir, errno);
  }

  if(mkdir_if_needed(app_dir) != 0 || mkdir_if_needed(sce_sys_dir) != 0) {
    bfpilot_log("launcher install mkdir failed errno=%d", errno);
    g_launcher_diag.launcher_install_rc = -errno;
    publish_launcher_diag();
    snprintf(msg, sizeof(msg), "mkdir failed errno %d", errno);
    bfpilot_notify("BFpilot app failed", msg);
    return -1;
  }

  if(write_file(param_path, bfpilot_param_json, bfpilot_param_json_size) != 0) {
    bfpilot_log("launcher install failed writing %s errno=%d",
                param_path, errno);
    g_launcher_diag.launcher_install_rc = -errno;
    publish_launcher_diag();
    bfpilot_notify("BFpilot app failed", "could not write param.json");
    return -1;
  }

  if(write_file(icon_path, bfpilot_icon0_png, bfpilot_icon0_png_size) != 0) {
    bfpilot_log("launcher install failed writing %s errno=%d",
                icon_path, errno);
    g_launcher_diag.launcher_install_rc = -errno;
    publish_launcher_diag();
    bfpilot_notify("BFpilot app failed", "could not write icon0.png");
    return -1;
  }

  err = install_app(&api, BFPILOT_APP_TITLE_ID, BFPILOT_APP_PARENT);
  if(err) {
    bfpilot_log("launcher install install_app failed rc=0x%08x", err);
    snprintf(msg, sizeof(msg), "register BFPL00001 failed 0x%08x", err);
    bfpilot_notify("BFpilot app failed", msg);
    return -1;
  }

  if(ensure_data_dir() != 0) {
    bfpilot_log("launcher install warning failed creating %s errno=%d",
                BFPILOT_DATA_DIR, errno);
  } else if(write_file(BFPILOT_MARKER_PATH, g_install_marker,
                 sizeof(g_install_marker) - 1) != 0) {
    bfpilot_log("launcher install warning failed writing %s errno=%d",
                BFPILOT_MARKER_PATH, errno);
  }

  bfpilot_notify("BFpilot app ready",
                 "Tile BFPL00001 opens http://127.0.0.1:5905/");
  return 1;
}
