/*
 * BFpilot - archive extraction worker.
 *
 * This file is used in two modes:
 * - linked into bfpilot.elf for the normal standalone extraction path
 * - built as bfpilot-archive-worker.elf for diagnostics and fallback testing
 */

#include "archive_worker.h"
#include "rar.hpp"
#include "ps5_7z.hpp"

#include "miniz.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#define BFPILOT_ARCHIVE_DIR "/data/bfpilot/archive"
#define BFPILOT_ARCHIVE_JOB BFPILOT_ARCHIVE_DIR "/job.ini"
#define BFPILOT_ARCHIVE_STATUS BFPILOT_ARCHIVE_DIR "/status.json"
#define BFPILOT_ARCHIVE_STATUS_TMP BFPILOT_ARCHIVE_DIR "/status.tmp"
#define BFPILOT_ARCHIVE_LOG BFPILOT_ARCHIVE_DIR "/archive-worker.log"
#define BFPILOT_ARCHIVE_DAEMON_LOCK BFPILOT_ARCHIVE_DIR "/daemon.lock"

#define ZIP_SIG_LOCAL 0x04034b50U
#define ZIP_SIG_CENTRAL 0x02014b50U
#define ZIP_SIG_END 0x06054b50U
#define ZIP_SIG_ZIP64_END 0x06064b50U
#define ZIP_SIG_ZIP64_LOCATOR 0x07064b50U

struct ArchiveConfig {
  std::string source;
  std::string destination;
  std::string password;
  unsigned threads;
  bool cleanup_partial;
};

struct WorkerStatus {
  std::string state;
  std::string archive_type;
  std::string source;
  std::string destination;
  std::string stage;
  std::string current;
  std::string error;
  unsigned long long total_bytes;
  unsigned long long bytes_written;
  unsigned long long files_done;
  unsigned long long total_files;
  int percent;
  int errno_code;
  int archive_exit_code;
  long started_ms;
};

static WorkerStatus g_status;
static long g_last_status_ms = 0;
static int g_last_progress = -1;

static long
NowMs(void) {
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

static std::string
Trim(const std::string &value) {
  size_t start = 0;
  while(start < value.size() && (unsigned char)value[start] <= ' ') start++;
  size_t end = value.size();
  while(end > start && (unsigned char)value[end - 1] <= ' ') end--;
  return value.substr(start, end - start);
}

static std::string
ToLowerAscii(const std::string &value) {
  std::string out = value;
  for(size_t i = 0; i < out.size(); i++) {
    if(out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
  }
  return out;
}

static bool
EndsWithNoCase(const std::string &value, const std::string &suffix) {
  if(value.size() < suffix.size()) return false;
  return ToLowerAscii(value.substr(value.size() - suffix.size())) ==
         ToLowerAscii(suffix);
}

static std::string
BaseName(const std::string &path) {
  size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

static std::string
DirName(const std::string &path) {
  size_t pos = path.find_last_of('/');
  if(pos == std::string::npos) return ".";
  if(pos == 0) return "/";
  return path.substr(0, pos);
}

static std::string
JoinPath(const std::string &left, const std::string &right) {
  if(left.empty() || right.empty() || right[0] == '/') return right;
  if(left[left.size() - 1] == '/') return left + right;
  return left + "/" + right;
}

static bool
MkdirAll(const std::string &path) {
  if(path.empty() || path == "/") return true;

  std::string cur;
  size_t pos = path[0] == '/' ? 1 : 0;
  if(path[0] == '/') cur = "/";

  while(pos <= path.size()) {
    size_t next = path.find('/', pos);
    std::string part =
        path.substr(pos, next == std::string::npos ? std::string::npos
                                                   : next - pos);
    if(!part.empty()) {
      if(!cur.empty() && cur[cur.size() - 1] != '/') cur += "/";
      cur += part;
      if(mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    if(next == std::string::npos) break;
    pos = next + 1;
  }
  return true;
}

static bool
PathExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

static bool
IsDirPath(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool
HasDotDotSegment(const std::string &path) {
  size_t pos = 0;
  while(pos <= path.size()) {
    size_t next = path.find('/', pos);
    std::string part =
        path.substr(pos, next == std::string::npos ? std::string::npos
                                                   : next - pos);
    if(part == "..") return true;
    if(next == std::string::npos) break;
    pos = next + 1;
  }
  return false;
}

static bool
StartsWithRoot(const std::string &path, const char *root) {
  size_t n = strlen(root);
  return path.compare(0, n, root) == 0 &&
         (path.size() == n || path[n] == '/');
}

static bool
IsAllowedArchivePath(const std::string &path) {
  if(path.empty() || path[0] != '/' || HasDotDotSegment(path)) return false;
  if(StartsWithRoot(path, "/data")) return true;
  for(int i = 0; i < 8; i++) {
    char root[32];
    snprintf(root, sizeof(root), "/mnt/usb%d", i);
    if(StartsWithRoot(path, root)) return true;
    snprintf(root, sizeof(root), "/mnt/ext%d", i);
    if(StartsWithRoot(path, root)) return true;
  }
  return false;
}

static bool
IsSafeMemberPath(const std::string &path) {
  if(path.empty() || path[0] == '/' || path[0] == '\\') return false;
  if(path.size() >= 2 && path[1] == ':') return false;

  size_t len = path.size();
  if(path[len - 1] == '/' || path[len - 1] == '\\') len--;
  if(len == 0) return false;

  size_t pos = 0;
  while(pos < len) {
    size_t next = path.find_first_of("/\\", pos);
    if(next == std::string::npos || next > len) next = len;
    std::string part = path.substr(pos, next - pos);
    if(part.empty() || part == "." || part == "..") return false;
    if(next >= len) break;
    pos = next + 1;
  }
  return true;
}

static std::string
NormalizeMemberPath(const std::string &path) {
  std::string out = path;
  for(size_t i = 0; i < out.size(); i++) {
    if(out[i] == '\\') out[i] = '/';
  }
  return out;
}

static std::string
JsonEscape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for(size_t i = 0; i < value.size(); i++) {
    unsigned char ch = (unsigned char)value[i];
    switch(ch) {
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if(ch < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", ch);
        out += buf;
      } else {
        out += (char)ch;
      }
      break;
    }
  }
  return out;
}

static bool
WriteAll(int fd, const void *data, size_t size) {
  const char *p = (const char *)data;
  while(size > 0) {
    ssize_t n = write(fd, p, size);
    if(n < 0) {
      if(errno == EINTR) continue;
      return false;
    }
    if(n == 0) return false;
    p += n;
    size -= (size_t)n;
  }
  return true;
}

static bool
ReadExact(int fd, void *data, size_t size) {
  char *p = (char *)data;
  while(size > 0) {
    ssize_t n = read(fd, p, size);
    if(n < 0) {
      if(errno == EINTR) continue;
      return false;
    }
    if(n == 0) return false;
    p += n;
    size -= (size_t)n;
  }
  return true;
}

static bool
ReadExactAt(int fd, unsigned long long offset, void *data, size_t size) {
  if(lseek(fd, (off_t)offset, SEEK_SET) < 0) return false;
  return ReadExact(fd, data, size);
}

static void
LogLine(const char *fmt, ...) {
  if(!MkdirAll(BFPILOT_ARCHIVE_DIR)) return;

  int fd = open(BFPILOT_ARCHIVE_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if(fd < 0) return;

  char msg[2048];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if(n > 0) {
    size_t len = (size_t)std::min(n, (int)sizeof(msg) - 2);
    msg[len++] = '\n';
    WriteAll(fd, msg, len);
  }
  close(fd);
}

static void
WriteStatus(bool force) {
  long now = NowMs();
  if(!force && now > 0 && g_last_status_ms > 0 &&
     now - g_last_status_ms < 750) {
    return;
  }
  g_last_status_ms = now;

  if(!MkdirAll(BFPILOT_ARCHIVE_DIR)) return;

  long elapsed_ms =
      g_status.started_ms > 0 && now > g_status.started_ms
          ? now - g_status.started_ms
          : 0;
  double mbps =
      elapsed_ms > 0
          ? ((double)g_status.bytes_written * 1000.0 / (double)elapsed_ms) /
                (1024.0 * 1024.0)
          : 0.0;

  char body[4096];
#ifdef BFPILOT_ARCHIVE_NO_MAIN
  const char *worker_name = "bfpilot-integrated-archive";
  const char *requires_injection = "false";
#else
  const char *worker_name = "bfpilot-archive-worker";
  const char *requires_injection = "true";
#endif
  int n = snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"worker\":\"%s\","
      "\"state\":\"%s\",\"archiveType\":\"%s\","
      "\"source\":\"%s\",\"destination\":\"%s\",\"stage\":\"%s\","
      "\"current\":\"%s\",\"error\":\"%s\","
      "\"percent\":%d,\"totalBytes\":%llu,\"bytesWritten\":%llu,"
      "\"filesDone\":%llu,\"totalFiles\":%llu,"
      "\"elapsedMs\":%ld,\"averageMBps\":%.2f,\"errno\":%d,"
      "\"archiveExitCode\":%d,"
      "\"requiresInjection\":%s}",
      worker_name,
      JsonEscape(g_status.state).c_str(),
      JsonEscape(g_status.archive_type).c_str(),
      JsonEscape(g_status.source).c_str(),
      JsonEscape(g_status.destination).c_str(),
      JsonEscape(g_status.stage).c_str(),
      JsonEscape(g_status.current).c_str(),
      JsonEscape(g_status.error).c_str(),
      g_status.percent, g_status.total_bytes, g_status.bytes_written,
      g_status.files_done, g_status.total_files, elapsed_ms, mbps,
      g_status.errno_code, g_status.archive_exit_code, requires_injection);
  if(n < 0) return;
  size_t len = (size_t)std::min(n, (int)sizeof(body) - 1);

  int fd = open(BFPILOT_ARCHIVE_STATUS_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) return;
  bool ok = WriteAll(fd, body, len);
  close(fd);
  if(ok) rename(BFPILOT_ARCHIVE_STATUS_TMP, BFPILOT_ARCHIVE_STATUS);
}

static void
SetStatus(const std::string &state, const std::string &current, bool force) {
  g_status.state = state;
  g_status.current = current;
  WriteStatus(force);
}

extern "C" void
Ps5UnrarProgress(int percent) {
  if(percent < 0) return;
  if(percent > 100) percent = 100;
  if(percent == g_last_progress) {
    WriteStatus(false);
    return;
  }
  g_last_progress = percent;
  g_status.percent = percent;
  WriteStatus(false);
}

extern "C" void
Ps5ArchiveProgress64(unsigned long long completed, unsigned long long total) {
  if(total > 0) {
    g_status.total_bytes = total;
    g_status.bytes_written = completed > total ? total : completed;
    g_status.percent = (int)((g_status.bytes_written * 100ULL) / total);
    if(g_status.percent > 100) g_status.percent = 100;
  }
  WriteStatus(false);
}

static bool
ReadSmallFile(const std::string &path, std::string &data, size_t max_size) {
  int fd = open(path.c_str(), O_RDONLY);
  if(fd < 0) return false;
  data.clear();
  char buf[4096];
  while(data.size() < max_size) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if(n < 0) {
      if(errno == EINTR) continue;
      close(fd);
      return false;
    }
    if(n == 0) break;
    data.append(buf, (size_t)n);
  }
  close(fd);
  return true;
}

static bool
LoadConfig(ArchiveConfig &cfg, std::string &error) {
  cfg.source.clear();
  cfg.destination.clear();
  cfg.password.clear();
  cfg.threads = 0;
  cfg.cleanup_partial = false;

  std::string data;
  if(!ReadSmallFile(BFPILOT_ARCHIVE_JOB, data, 64 * 1024)) {
    error = "failed to read " BFPILOT_ARCHIVE_JOB;
    return false;
  }

  size_t pos = 0;
  while(pos < data.size()) {
    size_t end = data.find('\n', pos);
    std::string line =
        data.substr(pos, end == std::string::npos ? std::string::npos
                                                  : end - pos);
    pos = end == std::string::npos ? data.size() : end + 1;
    line = Trim(line);
    if(line.empty() || line[0] == '#' || line[0] == ';') continue;
    size_t eq = line.find('=');
    if(eq == std::string::npos) continue;
    std::string key = Trim(line.substr(0, eq));
    std::string value = Trim(line.substr(eq + 1));
    if(key == "source") cfg.source = value;
    else if(key == "destination") cfg.destination = value;
    else if(key == "password") cfg.password = value;
    else if(key == "threads") cfg.threads = (unsigned)strtoul(value.c_str(), NULL, 10);
    else if(key == "cleanupPartial") {
      cfg.cleanup_partial =
          value == "1" || value == "true" || value == "yes" || value == "on";
    }
  }

  if(!IsAllowedArchivePath(cfg.source) ||
     !IsAllowedArchivePath(cfg.destination)) {
    error = "archive job path is outside allowed roots";
    return false;
  }
  if(cfg.source == cfg.destination) {
    error = "source and destination are the same";
    return false;
  }
  if(cfg.source.empty() || cfg.destination.empty()) {
    error = "archive job is missing source or destination";
    return false;
  }
  if(cfg.threads > 1) {
    LogLine("requested threads=%u clamped to 1 for PS5 stability",
            cfg.threads);
  }
  cfg.threads = 1;
  return true;
}

enum ArchiveType {
  ARCHIVE_UNKNOWN,
  ARCHIVE_RAR,
  ARCHIVE_7Z,
  ARCHIVE_ZIP
};

static const char *
ArchiveTypeName(ArchiveType type) {
  switch(type) {
  case ARCHIVE_RAR: return "rar";
  case ARCHIVE_7Z: return "7z";
  case ARCHIVE_ZIP: return "zip";
  default: return "unknown";
  }
}

static ArchiveType
DetectArchiveType(const std::string &path) {
  unsigned char magic[8] = {0};
  size_t magic_size = 0;
  int fd = open(path.c_str(), O_RDONLY);
  if(fd >= 0) {
    ssize_t n = read(fd, magic, sizeof(magic));
    if(n > 0) magic_size = (size_t)n;
    close(fd);
  }
  if(magic_size >= 7 && magic[0] == 0x52 && magic[1] == 0x61 &&
     magic[2] == 0x72 && magic[3] == 0x21 && magic[4] == 0x1a &&
     magic[5] == 0x07 && (magic[6] == 0x00 || magic[6] == 0x01)) {
    return ARCHIVE_RAR;
  }
  if(magic_size >= 6 && magic[0] == 0x37 && magic[1] == 0x7a &&
     magic[2] == 0xbc && magic[3] == 0xaf && magic[4] == 0x27 &&
     magic[5] == 0x1c) {
    return ARCHIVE_7Z;
  }
  if(magic_size >= 4 && magic[0] == 'P' && magic[1] == 'K' &&
     (magic[2] == 3 || magic[2] == 5 || magic[2] == 7) &&
     (magic[3] == 4 || magic[3] == 6 || magic[3] == 8)) {
    return ARCHIVE_ZIP;
  }

  std::string name = ToLowerAscii(BaseName(path));
  if(EndsWithNoCase(name, ".rar") ||
     (name.size() > 4 && name[name.size() - 4] == '.' &&
      name[name.size() - 3] == 'r' &&
      name[name.size() - 2] >= '0' && name[name.size() - 2] <= '9' &&
      name[name.size() - 1] >= '0' && name[name.size() - 1] <= '9')) {
    return ARCHIVE_RAR;
  }
  if(EndsWithNoCase(name, ".7z") || EndsWithNoCase(name, ".7z.001")) {
    return ARCHIVE_7Z;
  }
  if(EndsWithNoCase(name, ".zip")) return ARCHIVE_ZIP;
  return ARCHIVE_UNKNOWN;
}

static const char *
RarExitReason(RAR_EXIT code) {
  switch(code) {
  case RARX_SUCCESS: return "success";
  case RARX_WARNING: return "warning";
  case RARX_FATAL: return "fatal extraction error";
  case RARX_CRC: return "checksum or password error";
  case RARX_LOCK: return "archive lock error";
  case RARX_WRITE: return "write error";
  case RARX_OPEN: return "open error";
  case RARX_USERERROR: return "bad command or config";
  case RARX_MEMORY: return "out of memory";
  case RARX_CREATE: return "file create error";
  case RARX_NOFILES: return "no files extracted";
  case RARX_BADPWD: return "bad password";
  case RARX_READ: return "read error";
  case RARX_BADARC: return "bad archive";
  case RARX_USERBREAK: return "user break";
  }
  return "unknown error";
}

static std::string
RarExitDiagnostic(RAR_EXIT code, bool password_provided) {
  switch(code) {
  case RARX_BADPWD:
  case RARX_CRC:
    return password_provided
               ? "bad RAR password or checksum error"
               : "RAR password required or archive checksum error";
  case RARX_USERBREAK:
    return password_provided
               ? "RAR extraction aborted; check password and multipart volumes"
               : "RAR password required or next multipart volume is missing";
  case RARX_OPEN:
  case RARX_READ:
    return "RAR open/read error; check that every multipart volume is present next to the .rar";
  default:
    return RarExitReason(code);
  }
}

static int
RunUnrarExtract(const std::string &archive_path, const std::string &dest_path,
                const std::string &password, unsigned threads) {
  ErrHandler.Clean();
  ErrHandler.SetSignalHandlers(false);

  std::unique_ptr<CommandData> cmd(new CommandData);
  cmd->Command = L"X";
  CharToWide(archive_path, cmd->ArcName);
  CharToWide(dest_path, cmd->ExtrPath);
  AddEndSlash(cmd->ExtrPath);
  cmd->AddArcName(cmd->ArcName);
  cmd->FileArgs.AddString(MASKALL);
  cmd->AllYes = true;
  cmd->Overwrite = OVERWRITE_ALL;
  cmd->DisableCopyright = true;
  cmd->DisableDone = true;
  cmd->DisableNames = true;
  if(threads > 0) cmd->Threads = std::min<uint>(threads, MaxPoolThreads);

  if(!password.empty()) {
    std::wstring password_w;
    CharToWide(password, password_w);
    cmd->Password.Set(password_w.c_str());
  }

  uiInit(SOUND_NOTIFY_OFF);

  try {
    CmdExtract extract(cmd.get());
    extract.DoExtract();
  } catch(RAR_EXIT err_code) {
    ErrHandler.SetErrorCode(err_code);
  } catch(std::bad_alloc &) {
    ErrHandler.SetErrorCode(RARX_MEMORY);
  } catch(std::length_error &) {
    ErrHandler.SetErrorCode(RARX_MEMORY);
  } catch(...) {
    ErrHandler.SetErrorCode(RARX_FATAL);
  }

  ErrHandler.MainExit = true;
  return ErrHandler.GetErrorCode();
}

static unsigned short
ReadLe16(const unsigned char *p) {
  return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned int
ReadLe32(const unsigned char *p) {
  return ((unsigned int)p[0]) | ((unsigned int)p[1] << 8) |
         ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned long long
ReadLe64(const unsigned char *p) {
  unsigned long long lo = ReadLe32(p);
  unsigned long long hi = ReadLe32(p + 4);
  return lo | (hi << 32);
}

class ZipCrypto {
  unsigned int keys[3];

  static unsigned int crc_update(unsigned int crc, unsigned char ch) {
    static unsigned int table[256];
    static bool ready = false;
    if(!ready) {
      for(unsigned int i = 0; i < 256; i++) {
        unsigned int c = i;
        for(unsigned int j = 0; j < 8; j++) {
          c = (c & 1U) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
      }
      ready = true;
    }
    return (crc >> 8) ^ table[(crc ^ ch) & 0xffU];
  }

  void update(unsigned char ch) {
    keys[0] = crc_update(keys[0], ch);
    keys[1] = keys[1] + (keys[0] & 0xff);
    keys[1] = keys[1] * 134775813U + 1U;
    unsigned char high = (unsigned char)(keys[1] >> 24);
    keys[2] = crc_update(keys[2], high);
  }

  unsigned char decrypt_byte(void) const {
    unsigned int t = keys[2] | 2U;
    return (unsigned char)(((t * (t ^ 1U)) >> 8) & 0xff);
  }

public:
  ZipCrypto(const std::string &password) {
    keys[0] = 305419896U;
    keys[1] = 591751049U;
    keys[2] = 878082192U;
    for(size_t i = 0; i < password.size(); i++) {
      update((unsigned char)password[i]);
    }
  }

  unsigned char decrypt(unsigned char ch) {
    unsigned char plain = (unsigned char)(ch ^ decrypt_byte());
    update(plain);
    return plain;
  }

  void decrypt_buffer(unsigned char *buf, size_t size) {
    for(size_t i = 0; i < size; i++) buf[i] = decrypt(buf[i]);
  }
};

struct ZipEntry {
  std::string name;
  unsigned short flags;
  unsigned short method;
  unsigned short mod_time;
  unsigned int crc32;
  unsigned long long compressed_size;
  unsigned long long uncompressed_size;
  unsigned long long local_offset;
  bool is_dir;
  bool encrypted;
  bool aes_encrypted;
};

static bool
ParseZip64Extra(const std::vector<unsigned char> &extra, ZipEntry &entry,
                bool need_uncomp, bool need_comp, bool need_offset) {
  size_t pos = 0;
  while(pos + 4 <= extra.size()) {
    unsigned short header_id = ReadLe16(&extra[pos]);
    unsigned short data_size = ReadLe16(&extra[pos + 2]);
    pos += 4;
    if(pos + data_size > extra.size()) return false;
    if(header_id == 0x0001) {
      size_t p = pos;
      if(need_uncomp) {
        if(p + 8 > pos + data_size) return false;
        entry.uncompressed_size = ReadLe64(&extra[p]);
        p += 8;
      }
      if(need_comp) {
        if(p + 8 > pos + data_size) return false;
        entry.compressed_size = ReadLe64(&extra[p]);
        p += 8;
      }
      if(need_offset) {
        if(p + 8 > pos + data_size) return false;
        entry.local_offset = ReadLe64(&extra[p]);
      }
      return true;
    }
    pos += data_size;
  }
  return !(need_uncomp || need_comp || need_offset);
}

static bool
ZipExtraHasAes(const std::vector<unsigned char> &extra) {
  size_t pos = 0;
  while(pos + 4 <= extra.size()) {
    unsigned short header_id = ReadLe16(&extra[pos]);
    unsigned short data_size = ReadLe16(&extra[pos + 2]);
    pos += 4;
    if(pos + data_size > extra.size()) return false;
    if(header_id == 0x9901) return true;
    pos += data_size;
  }
  return false;
}

static bool
FindZipCentralDir(int fd, unsigned long long file_size,
                  unsigned long long &central_offset,
                  unsigned long long &central_size,
                  unsigned long long &entry_count,
                  std::string &error) {
  size_t tail_size =
      (size_t)std::min<unsigned long long>(file_size, 22ULL + 65535ULL + 128ULL);
  std::vector<unsigned char> tail(tail_size);
  if(!ReadExactAt(fd, file_size - tail_size, tail.data(), tail_size)) {
    error = "failed to read zip end of central directory";
    return false;
  }

  ssize_t eocd = -1;
  for(ssize_t i = (ssize_t)tail_size - 22; i >= 0; i--) {
    if(ReadLe32(&tail[(size_t)i]) == ZIP_SIG_END) {
      eocd = i;
      break;
    }
  }
  if(eocd < 0) {
    error = "zip end of central directory not found";
    return false;
  }

  const unsigned char *p = &tail[(size_t)eocd];
  unsigned int entries16 = ReadLe16(p + 10);
  unsigned int size32 = ReadLe32(p + 12);
  unsigned int offset32 = ReadLe32(p + 16);
  central_size = size32;
  central_offset = offset32;
  entry_count = entries16;

  bool needs_zip64 = entries16 == 0xffffU || size32 == 0xffffffffU ||
                     offset32 == 0xffffffffU;
  if(!needs_zip64) return true;

  unsigned long long eocd_abs = file_size - tail_size + (unsigned long long)eocd;
  if(eocd_abs < 20) {
    error = "zip64 locator missing";
    return false;
  }
  unsigned char locator[20];
  if(!ReadExactAt(fd, eocd_abs - 20, locator, sizeof(locator)) ||
     ReadLe32(locator) != ZIP_SIG_ZIP64_LOCATOR) {
    error = "zip64 locator missing";
    return false;
  }
  unsigned long long zip64_eocd_offset = ReadLe64(locator + 8);
  unsigned char zip64[56];
  if(!ReadExactAt(fd, zip64_eocd_offset, zip64, sizeof(zip64)) ||
     ReadLe32(zip64) != ZIP_SIG_ZIP64_END) {
    error = "zip64 end of central directory missing";
    return false;
  }
  entry_count = ReadLe64(zip64 + 32);
  central_size = ReadLe64(zip64 + 40);
  central_offset = ReadLe64(zip64 + 48);
  return true;
}

static bool
ReadZipEntries(int fd, unsigned long long central_offset,
               unsigned long long entry_count, std::vector<ZipEntry> &entries,
               std::string &error) {
  unsigned long long offset = central_offset;
  for(unsigned long long i = 0; i < entry_count; i++) {
    unsigned char hdr[46];
    if(!ReadExactAt(fd, offset, hdr, sizeof(hdr)) ||
       ReadLe32(hdr) != ZIP_SIG_CENTRAL) {
      error = "bad zip central directory entry";
      return false;
    }
    ZipEntry entry;
    entry.flags = ReadLe16(hdr + 8);
    entry.method = ReadLe16(hdr + 10);
    entry.mod_time = ReadLe16(hdr + 12);
    entry.crc32 = ReadLe32(hdr + 16);
    entry.compressed_size = ReadLe32(hdr + 20);
    entry.uncompressed_size = ReadLe32(hdr + 24);
    unsigned short name_len = ReadLe16(hdr + 28);
    unsigned short extra_len = ReadLe16(hdr + 30);
    unsigned short comment_len = ReadLe16(hdr + 32);
    entry.local_offset = ReadLe32(hdr + 42);
    entry.encrypted = (entry.flags & 1) != 0;

    std::vector<unsigned char> name_buf(name_len);
    std::vector<unsigned char> extra(extra_len);
    if(name_len > 0 &&
       !ReadExactAt(fd, offset + 46, name_buf.data(), name_len)) {
      error = "failed to read zip file name";
      return false;
    }
    if(extra_len > 0 &&
       !ReadExactAt(fd, offset + 46 + name_len, extra.data(), extra_len)) {
      error = "failed to read zip extra data";
      return false;
    }
    entry.name.assign((const char *)name_buf.data(), name_buf.size());
    entry.name = NormalizeMemberPath(entry.name);
    entry.is_dir = !entry.name.empty() && entry.name[entry.name.size() - 1] == '/';
    entry.aes_encrypted = entry.encrypted &&
                          (entry.method == 99 || ZipExtraHasAes(extra));

    bool need_uncomp = entry.uncompressed_size == 0xffffffffULL;
    bool need_comp = entry.compressed_size == 0xffffffffULL;
    bool need_offset = entry.local_offset == 0xffffffffULL;
    if((need_uncomp || need_comp || need_offset) &&
       !ParseZip64Extra(extra, entry, need_uncomp, need_comp, need_offset)) {
      error = "bad or missing zip64 extra data";
      return false;
    }

    if(!IsSafeMemberPath(entry.name)) {
      error = "unsafe zip member path: " + entry.name;
      return false;
    }

    entries.push_back(entry);
    offset += 46ULL + name_len + extra_len + comment_len;
  }
  return true;
}

static bool
OpenZipEntryData(int fd, const ZipEntry &entry, unsigned long long &data_offset,
                 std::string &error) {
  unsigned char hdr[30];
  if(!ReadExactAt(fd, entry.local_offset, hdr, sizeof(hdr)) ||
     ReadLe32(hdr) != ZIP_SIG_LOCAL) {
    error = "bad zip local file header";
    return false;
  }
  unsigned short name_len = ReadLe16(hdr + 26);
  unsigned short extra_len = ReadLe16(hdr + 28);
  data_offset = entry.local_offset + 30ULL + name_len + extra_len;
  return true;
}

static bool
WriteZipStoredFile(int zip_fd, int out_fd, ZipEntry &entry,
                   unsigned long long data_offset, ZipCrypto *crypto,
                   std::string &error) {
  unsigned char buf[128 * 1024];
  unsigned long long remaining = entry.compressed_size;
  mz_ulong crc = MZ_CRC32_INIT;
  unsigned long long written = 0;

  while(remaining > 0) {
    size_t want =
        (size_t)std::min<unsigned long long>(remaining, sizeof(buf));
    if(!ReadExactAt(zip_fd, data_offset, buf, want)) {
      error = "failed to read stored zip data";
      return false;
    }
    if(crypto) crypto->decrypt_buffer(buf, want);
    if(!WriteAll(out_fd, buf, want)) {
      error = "failed to write extracted zip file";
      return false;
    }
    crc = mz_crc32(crc, buf, want);
    data_offset += want;
    remaining -= want;
    written += want;
    g_status.bytes_written += want;
    g_status.percent = g_status.total_bytes > 0
                           ? (int)((g_status.bytes_written * 100ULL) /
                                   g_status.total_bytes)
                           : g_status.percent;
    WriteStatus(false);
  }

  if(written != entry.uncompressed_size) {
    error = "stored zip size mismatch";
    return false;
  }
  if((unsigned int)crc != entry.crc32) {
    error = "zip checksum or password error";
    return false;
  }
  return true;
}

static bool
WriteZipDeflatedFile(int zip_fd, int out_fd, ZipEntry &entry,
                     unsigned long long data_offset, ZipCrypto *crypto,
                     std::string &error) {
  unsigned char inbuf[128 * 1024];
  unsigned char outbuf[128 * 1024];
  unsigned long long remaining = entry.compressed_size;
  unsigned long long written = 0;
  mz_ulong crc = MZ_CRC32_INIT;

  mz_stream stream;
  memset(&stream, 0, sizeof(stream));
  int zrc = mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS);
  if(zrc != MZ_OK) {
    error = "zip deflate init failed";
    return false;
  }

  bool ok = true;
  bool done = false;
  while(!done) {
    if(stream.avail_in == 0 && remaining > 0) {
      size_t want =
          (size_t)std::min<unsigned long long>(remaining, sizeof(inbuf));
      if(!ReadExactAt(zip_fd, data_offset, inbuf, want)) {
        error = "failed to read deflated zip data";
        ok = false;
        break;
      }
      if(crypto) crypto->decrypt_buffer(inbuf, want);
      data_offset += want;
      remaining -= want;
      stream.next_in = inbuf;
      stream.avail_in = (mz_uint)want;
    }

    stream.next_out = outbuf;
    stream.avail_out = sizeof(outbuf);
    zrc = mz_inflate(&stream, remaining == 0 ? MZ_FINISH : MZ_NO_FLUSH);
    size_t produced = sizeof(outbuf) - stream.avail_out;
    if(produced > 0) {
      if(!WriteAll(out_fd, outbuf, produced)) {
        error = "failed to write extracted zip file";
        ok = false;
        break;
      }
      crc = mz_crc32(crc, outbuf, produced);
      written += produced;
      g_status.bytes_written += produced;
      g_status.percent = g_status.total_bytes > 0
                             ? (int)((g_status.bytes_written * 100ULL) /
                                     g_status.total_bytes)
                             : g_status.percent;
      WriteStatus(false);
    }

    if(zrc == MZ_STREAM_END) {
      done = true;
    } else if(zrc != MZ_OK && zrc != MZ_BUF_ERROR) {
      error = "zip deflate or password error";
      ok = false;
      break;
    } else if(remaining == 0 && stream.avail_in == 0 && produced == 0) {
      error = "zip deflate ended unexpectedly";
      ok = false;
      break;
    }
  }

  mz_inflateEnd(&stream);
  if(!ok) return false;
  if(written != entry.uncompressed_size) {
    error = "deflated zip size mismatch";
    return false;
  }
  if((unsigned int)crc != entry.crc32) {
    error = "zip checksum or password error";
    return false;
  }
  return true;
}

static bool
ExtractOneZipEntry(int zip_fd, ZipEntry &entry, const std::string &dest,
                   const std::string &password, std::string &error) {
  std::string out_path = JoinPath(dest, entry.name);
  g_status.current = entry.name;

  if(entry.is_dir) {
    if(!MkdirAll(out_path)) {
      error = "failed to create zip directory: " + entry.name;
      return false;
    }
    g_status.files_done++;
    WriteStatus(false);
    return true;
  }

  if(entry.aes_encrypted) {
    error = "zip AES encryption is not supported yet";
    return false;
  }
  if(entry.encrypted && password.empty()) {
    error = "zip entry needs a password";
    return false;
  }
  if(entry.method != 0 && entry.method != MZ_DEFLATED) {
    char buf[96];
    snprintf(buf, sizeof(buf), "unsupported zip compression method %u",
             (unsigned)entry.method);
    error = buf;
    return false;
  }

  unsigned long long data_offset = 0;
  if(!OpenZipEntryData(zip_fd, entry, data_offset, error)) return false;

  ZipCrypto zip_crypto(password);
  ZipCrypto *crypto = entry.encrypted ? &zip_crypto : NULL;
  if(entry.encrypted) {
    if(entry.compressed_size < 12) {
      error = "bad encrypted zip entry";
      return false;
    }
    unsigned char header[12];
    if(!ReadExactAt(zip_fd, data_offset, header, sizeof(header))) {
      error = "failed to read encrypted zip header";
      return false;
    }
    crypto->decrypt_buffer(header, sizeof(header));
    unsigned char crc_check = (unsigned char)(entry.crc32 >> 24);
    unsigned char time_check = (unsigned char)(entry.mod_time >> 8);
    if(header[11] != crc_check && header[11] != time_check) {
      error = "bad zip password";
      return false;
    }
    data_offset += 12;
    entry.compressed_size -= 12;
  }

  if(!MkdirAll(DirName(out_path))) {
    error = "failed to create zip output directory";
    return false;
  }

  int out_fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(out_fd < 0) {
    error = "failed to create zip output file";
    return false;
  }

  bool ok = false;
  if(entry.method == 0) {
    ok = WriteZipStoredFile(zip_fd, out_fd, entry, data_offset, crypto, error);
  } else {
    ok = WriteZipDeflatedFile(zip_fd, out_fd, entry, data_offset, crypto, error);
  }

  if(close(out_fd) != 0 && ok) {
    error = "failed to close zip output file";
    ok = false;
  }
  if(ok) g_status.files_done++;
  WriteStatus(false);
  return ok;
}

static bool
RunZipExtract(const std::string &archive_path, const std::string &dest_path,
              const std::string &password, std::string &error) {
  int fd = open(archive_path.c_str(), O_RDONLY);
  if(fd < 0) {
    error = "failed to open zip archive";
    return false;
  }

  struct stat st;
  if(fstat(fd, &st) != 0) {
    close(fd);
    error = "failed to stat zip archive";
    return false;
  }

  unsigned long long central_offset = 0;
  unsigned long long central_size = 0;
  unsigned long long entry_count = 0;
  if(!FindZipCentralDir(fd, (unsigned long long)st.st_size, central_offset,
                        central_size, entry_count, error)) {
    close(fd);
    return false;
  }

  std::vector<ZipEntry> entries;
  entries.reserve((size_t)std::min<unsigned long long>(entry_count, 4096ULL));
  if(!ReadZipEntries(fd, central_offset, entry_count, entries, error)) {
    close(fd);
    return false;
  }

  (void)central_size;
  g_status.total_files = entries.size();
  g_status.total_bytes = 0;
  for(size_t i = 0; i < entries.size(); i++) {
    if(!entries[i].is_dir) g_status.total_bytes += entries[i].uncompressed_size;
  }
  WriteStatus(true);

  if(!MkdirAll(dest_path)) {
    close(fd);
    error = "failed to create zip destination";
    return false;
  }

  for(size_t i = 0; i < entries.size(); i++) {
    if(!ExtractOneZipEntry(fd, entries[i], dest_path, password, error)) {
      close(fd);
      return false;
    }
  }

  close(fd);
  return true;
}

static int
RunArchive(const ArchiveConfig &cfg, ArchiveType type, std::string &error) {
  if(type == ARCHIVE_RAR) {
    g_status.total_bytes = 0;
    g_status.total_files = 0;
    WriteStatus(true);
    errno = 0;
    int code = RunUnrarExtract(cfg.source, g_status.stage, cfg.password,
                               cfg.threads);
    if(code != RARX_SUCCESS && code != RARX_WARNING) {
      error = RarExitDiagnostic((RAR_EXIT)code, !cfg.password.empty());
      return code;
    }
    g_status.percent = 100;
    return 0;
  }

  if(type == ARCHIVE_7Z) {
    g_status.total_bytes = 0;
    g_status.total_files = 0;
    WriteStatus(true);
    errno = 0;
    SevenZExtractResult result =
        Run7zExtract(cfg.source, g_status.stage, cfg.password, cfg.threads);
    if(result.code != RARX_SUCCESS && result.code != RARX_WARNING) {
      error = result.reason;
      if(!result.missing_volume.empty()) {
        error += ": ";
        error += result.missing_volume;
      }
      return result.code;
    }
    g_status.percent = 100;
    return 0;
  }

  if(type == ARCHIVE_ZIP) {
    errno = 0;
    return RunZipExtract(cfg.source, g_status.stage, cfg.password, error) ? 0
                                                                         : 1;
  }

  error = "unsupported archive type";
  return 1;
}

extern "C" int
bfpilot_archive_run_prepared_job(void) {
  g_status.state = "starting";
  g_status.started_ms = NowMs();
  g_status.percent = 0;
  g_status.archive_exit_code = 0;
  g_status.errno_code = 0;
  g_status.total_bytes = 0;
  g_status.bytes_written = 0;
  g_status.files_done = 0;
  g_status.total_files = 0;
  g_status.source.clear();
  g_status.destination.clear();
  g_status.stage.clear();
  g_status.current.clear();
  g_status.error.clear();
  g_status.archive_type.clear();
  g_last_status_ms = 0;
  g_last_progress = -1;
  WriteStatus(true);
  LogLine("archive job entry reached");

  ArchiveConfig cfg;
  std::string error;
  if(!LoadConfig(cfg, error)) {
    g_status.state = "error";
    g_status.error = error;
    g_status.errno_code = errno;
    WriteStatus(true);
    LogLine("config error: %s", error.c_str());
    return 2;
  }

  g_status.source = cfg.source;
  g_status.destination = cfg.destination;

  struct stat src_st;
  if(stat(cfg.source.c_str(), &src_st) != 0 || !S_ISREG(src_st.st_mode)) {
    g_status.state = "error";
    g_status.error = "source archive is not a regular file";
    g_status.errno_code = errno;
    WriteStatus(true);
    LogLine("source stat failed path=%s errno=%d", cfg.source.c_str(), errno);
    return 3;
  }

  if(PathExists(cfg.destination)) {
    g_status.state = "error";
    g_status.error = "destination already exists";
    g_status.errno_code = EEXIST;
    WriteStatus(true);
    LogLine("destination exists path=%s", cfg.destination.c_str());
    return 4;
  }

  std::string parent = DirName(cfg.destination);
  if(!IsDirPath(parent) && !MkdirAll(parent)) {
    g_status.state = "error";
    g_status.error = "failed to create destination parent";
    g_status.errno_code = errno;
    WriteStatus(true);
    LogLine("destination parent failed path=%s errno=%d", parent.c_str(), errno);
    return 5;
  }

  char stage_suffix[80];
  snprintf(stage_suffix, sizeof(stage_suffix), ".bfpilot-extracting-%ld-%ld",
           (long)getpid(), (long)time(NULL));
  g_status.stage = cfg.destination + stage_suffix;
  if(PathExists(g_status.stage)) {
    g_status.state = "error";
    g_status.error = "staging path already exists";
    g_status.errno_code = EEXIST;
    WriteStatus(true);
    LogLine("stage exists path=%s", g_status.stage.c_str());
    return 6;
  }
  if(!MkdirAll(g_status.stage)) {
    g_status.state = "error";
    g_status.error = "failed to create staging directory";
    g_status.errno_code = errno;
    WriteStatus(true);
    LogLine("stage mkdir failed path=%s errno=%d", g_status.stage.c_str(), errno);
    return 7;
  }

  ArchiveType type = DetectArchiveType(cfg.source);
  g_status.archive_type = ArchiveTypeName(type);
  SetStatus("running", "extracting", true);
  LogLine("extract start type=%s source=%s destination=%s stage=%s password=%s",
          ArchiveTypeName(type), cfg.source.c_str(), cfg.destination.c_str(),
          g_status.stage.c_str(), cfg.password.empty() ? "empty" : "provided");

  int rc = RunArchive(cfg, type, error);
  if(rc != 0) {
    g_status.state = "error";
    g_status.error = error.empty() ? "archive extraction failed" : error;
    g_status.errno_code = errno;
    g_status.archive_exit_code = rc;
    WriteStatus(true);
    LogLine("extract failed rc=%d errno=%d error=%s stage=%s", rc, errno,
            g_status.error.c_str(), g_status.stage.c_str());
    return 10 + rc;
  }

  SetStatus("finalizing", "renaming staging directory", true);
  if(PathExists(cfg.destination)) {
    g_status.state = "error";
    g_status.error = "destination appeared before finalize";
    g_status.errno_code = EEXIST;
    WriteStatus(true);
    LogLine("destination appeared before finalize path=%s",
            cfg.destination.c_str());
    return 20;
  }
  if(rename(g_status.stage.c_str(), cfg.destination.c_str()) != 0) {
    g_status.state = "error";
    g_status.error = "failed to finalize extraction";
    g_status.errno_code = errno;
    WriteStatus(true);
    LogLine("finalize failed stage=%s destination=%s errno=%d",
            g_status.stage.c_str(), cfg.destination.c_str(), errno);
    return 21;
  }

  g_status.stage.clear();
  g_status.current = cfg.destination;
  g_status.percent = 100;
  g_status.state = "done";
  WriteStatus(true);
  LogLine("extract done type=%s destination=%s bytes_written=%llu files=%llu",
          ArchiveTypeName(type), cfg.destination.c_str(),
          g_status.bytes_written, g_status.files_done);
  return 0;
}

static int
ArchiveDaemonMain(void) {
  if(!MkdirAll(BFPILOT_ARCHIVE_DIR)) {
    LogLine("archive daemon mkdir failed errno=%d", errno);
    return 2;
  }

  int lock_fd = open(BFPILOT_ARCHIVE_DAEMON_LOCK, O_WRONLY | O_CREAT, 0666);
  if(lock_fd < 0) {
    LogLine("archive daemon lock open failed errno=%d", errno);
    return 3;
  }
  if(flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    LogLine("archive daemon lock busy errno=%d", errno);
    close(lock_fd);
    return 4;
  }

  LogLine("archive daemon started pid=%ld", (long)getpid());
  std::string last_job;
  for(;;) {
    std::string status;
    bool prepared = ReadSmallFile(BFPILOT_ARCHIVE_STATUS, status, 32768) &&
                    status.find("\"state\":\"prepared\"") != std::string::npos;
    if(prepared) {
      std::string job;
      if(ReadSmallFile(BFPILOT_ARCHIVE_JOB, job, 32768) && job != last_job) {
        last_job = job;
        int rc = bfpilot_archive_run_prepared_job();
        LogLine("archive daemon job finished rc=%d", rc);
      }
    } else {
      last_job.clear();
    }
    usleep(250000);
  }
}

extern "C" int
bfpilot_archive_start_daemon(void) {
  pid_t child = fork();
  if(child < 0) {
    LogLine("archive daemon fork failed errno=%d", errno);
    return -errno;
  }
  if(child == 0) {
    int rc = ArchiveDaemonMain();
    _exit(rc & 0xff);
  }
  LogLine("archive daemon forked pid=%ld", (long)child);
  return (int)child;
}

#ifndef BFPILOT_ARCHIVE_NO_MAIN
int
main(void) {
  return bfpilot_archive_run_prepared_job();
}
#endif
