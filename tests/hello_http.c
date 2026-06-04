/*
 * BFpilot test payload - tiny HTTP status server.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "boot_marker.h"

#ifndef BFPILOT_HELLO_HTTP_PORT
#define BFPILOT_HELLO_HTTP_PORT 5906
#endif


static int
write_all(int fd, const void *data, size_t size) {
  const char *p = data;
  while(size > 0) {
    ssize_t wr = send(fd, p, size, 0);
    if(wr < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(wr == 0) return -1;
    p += wr;
    size -= (size_t)wr;
  }
  return 0;
}


static void
serve_client(int fd) {
  char req[512];
  ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
  if(n < 0) {
    close(fd);
    return;
  }
  req[n > 0 ? n : 0] = 0;

  const char body[] =
      "{\"ok\":true,\"name\":\"BFpilot hello_http\",\"port\":5906}";
  const char not_found[] = "{\"ok\":false,\"error\":\"not found\"}";
  char header[256];

  if(strstr(req, "GET /api/status ") || strstr(req, "GET /api/status?")) {
    int h = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Connection: close\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %lu\r\n\r\n",
                     (unsigned long)(sizeof(body) - 1));
    (void)write_all(fd, header, (size_t)h);
    (void)write_all(fd, body, sizeof(body) - 1);
  } else {
    int h = snprintf(header, sizeof(header),
                     "HTTP/1.1 404 Not Found\r\n"
                     "Connection: close\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %lu\r\n\r\n",
                     (unsigned long)(sizeof(not_found) - 1));
    (void)write_all(fd, header, (size_t)h);
    (void)write_all(fd, not_found, sizeof(not_found) - 1);
  }

  shutdown(fd, SHUT_RDWR);
  close(fd);
}


int
main(void) {
  bfpilot_boot_marker("tests/hello_http", BFPILOT_BUILD_MODE);
  signal(SIGPIPE, SIG_IGN);

  int srv = socket(AF_INET, SOCK_STREAM, 0);
  if(srv < 0) {
    printf("BFpilot hello_http socket failed errno=%d\n", errno);
    return 1;
  }

  int on = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(BFPILOT_HELLO_HTTP_PORT);

  if(bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    printf("BFpilot hello_http bind failed errno=%d\n", errno);
    close(srv);
    return 1;
  }

  if(listen(srv, 4) != 0) {
    printf("BFpilot hello_http listen failed errno=%d\n", errno);
    close(srv);
    return 1;
  }

  printf("BFpilot hello_http listening on port %u\n",
         (unsigned int)BFPILOT_HELLO_HTTP_PORT);

  for(;;) {
    int fd = accept(srv, NULL, NULL);
    if(fd < 0) {
      if(errno == EINTR) continue;
      printf("BFpilot hello_http accept failed errno=%d\n", errno);
      break;
    }
    serve_client(fd);
  }

  close(srv);
  return 0;
}
