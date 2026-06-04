/*
 * BFpilot test payload helpers.
 */

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BFPILOT_PROBE_DATA_ROOT "/data"
#define BFPILOT_PROBE_DATA_DIR "/data/BFpilot"


static int __attribute__((unused))
probe_mkdir_if_needed(const char *path) {
  if(mkdir(path, 0755) == 0) return 0;
  return errno == EEXIST ? 0 : -errno;
}


static void __attribute__((unused))
probe_ensure_data_dir(void) {
  (void)probe_mkdir_if_needed(BFPILOT_PROBE_DATA_ROOT);
  (void)probe_mkdir_if_needed(BFPILOT_PROBE_DATA_DIR);
}


static int __attribute__((unused))
probe_write_marker(const char *path, const char *payload_name) {
  char line[512];

  probe_ensure_data_dir();
  int n = snprintf(line, sizeof(line),
                   "%ld payload=%s pid=%ld entered main\n",
                   (long)time(NULL),
                   payload_name ? payload_name : "unknown",
                   (long)getpid());
  if(n < 0) return -EINVAL;
  if((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;

  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(fd < 0) return -errno;

  ssize_t wr = write(fd, line, (size_t)n);
  int saved_errno = errno;
  if(close(fd) != 0 && wr == n) return -errno;
  if(wr != n) return wr < 0 ? -saved_errno : -EIO;
  return 0;
}


static void __attribute__((unused))
probe_log(const char *path, const char *fmt, ...) {
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

  probe_ensure_data_dir();
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if(fd >= 0) {
    (void)write(fd, line, (size_t)n);
    close(fd);
  }
}
