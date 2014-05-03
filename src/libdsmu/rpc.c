#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "b64.h"
#include "mem.h"
#include "rpc.h"

// Socket state.
int serverfd;
struct addrinfo *resolvedAddr;
struct addrinfo hints;

// Send a message to the manager.
int sendman(char *str, int len) {
  // Acquire manager socket lock.
  return send(serverfd, str, len, 0);
}

// Initialize socket with manager.
// Return 0 on success.
int initsocks(void) {
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if (getaddrinfo(NULL, "4445", &hints, &resolvedAddr) < 0)
    return -2;

  serverfd = socket(resolvedAddr->ai_family, resolvedAddr->ai_socktype,
                    resolvedAddr->ai_protocol);
  if (serverfd < 0)
    return -2;

  if (connect(serverfd, resolvedAddr->ai_addr, resolvedAddr->ai_addrlen) < 0)
    return -2;

  return 0;
}

// Cleanup sockets.
int teardownsocks(void) {
  close(serverfd);
  return 0;
}

void confirminvalidate(int pgnum) {
  char msg[100] = {0};
  snprintf(msg, 100, "INVALIDATE CONFIRMATION %d", pgnum);
  sendman(msg, strlen(msg));
}

void confirminvalidate_encoded(int pgnum, char *pgb64) {
  char msg[10000] = {0};
  snprintf(msg, 100 + strlen(pgb64), "INVALIDATE CONFIRMATION %d %s", pgnum, pgb64);
  sendman(msg, strlen(msg));
}

// Handle invalidate messages.
// TODO: Hold a lock so that a write-fault won't randomly start writing here.
// BUT: The write-fault handler should release the lock immediately after it's
// done setting the page to writeable.
int invalidate(char *msg) {
  int err;
  char *spgnum = strstr(msg, " ") + 1;
  printf("PAGE NUMBER STRING: %s\n", spgnum);
  int pgnum = atoi(spgnum);
  printf("ATOI RETURNED: %d\n", pgnum);
  printf(">> Invalidate page number %d\n", pgnum);
  void *pg = (void *)PGNUM_TO_PGADDR((uintptr_t)pgnum);

  // If we don't need to reply with a b64-encoding of the page, just invalidate
  // and reply.
  if (strstr(msg, "PAGEDATA") == NULL) {
    printf("invalidation does not need a b64-encoding of the page\n");
    if ((err = mprotect(pg, 1, PROT_NONE)) != 0) {
      fprintf(stderr, "Invalidation of page addr %p failed with error %d\n", pg, err);
      return -1;
    }
    printf("Successfully invalidated page at addr %p\n", pg);
    confirminvalidate(pgnum);
    return 0;
  }

  // We need to reply with a b64-encoding of the page. Set to read-only, encode
  // the page, set to non-readable, non-writeable, and confirm with the
  // encoding. We need to hold a lock to prevent the page from becoming
  // writeable while we are encoding it. (Do we?)
  printf("invalidation needs a b64-encoding of the page\n");
  if (mprotect(pg, 1, PROT_READ) != 0) {
    fprintf(stderr, "Invalidation of page addr %p failed\n", pg);
    return -1;
  }
  printf("page is read-only\n");
  char pgb64[PG_SIZE * 2] = {0};
  b64encode((const char *)pg, PG_SIZE, pgb64);
  printf("page is b64-encoded\n");
  if (mprotect(pg, 1, PROT_NONE) != 0) {
    fprintf(stderr, "Invalidation of page addr %p failed\n", pg);
    return -1;
  }
  printf("page is unreadable, unwriteable\n");
  confirminvalidate_encoded(pgnum, pgb64);
  return 0;
}

// Handle newly arrived messages.
int dispatch(char *msg) {
  if (strstr(msg, "INVALIDATE") != NULL) {
    invalidate(msg);
  }
  return 0;
}

// Listen for manager messages and dispatch them.
void *listenman(void *ptr) {
  printf("Listening...\n");
  while (1) {
    char buf[7000] = {0};
    recv(serverfd, (void *)buf, 7000, 0);
    dispatch(buf);
  }
}

