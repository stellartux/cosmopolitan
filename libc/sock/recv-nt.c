/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│vi: set net ft=c ts=2 sts=2 sw=2 fenc=utf-8                                :vi│
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2020 Justine Alexandra Roberts Tunney                              │
│                                                                              │
│ Permission to use, copy, modify, and/or distribute this software for         │
│ any purpose with or without fee is hereby granted, provided that the         │
│ above copyright notice and this permission notice appear in all copies.      │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
│ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
│ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
│ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
│ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
│ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
│ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
│ PERFORMANCE OF THIS SOFTWARE.                                                │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "libc/assert.h"
#include "libc/calls/internal.h"
#include "libc/calls/struct/fd.internal.h"
#include "libc/errno.h"
#include "libc/intrin/strace.internal.h"
#include "libc/nt/struct/iovec.h"
#include "libc/nt/struct/overlapped.h"
#include "libc/nt/winsock.h"
#include "libc/sock/internal.h"
#include "libc/sock/syscall_fd.internal.h"
#include "libc/sysv/errfuns.h"

textwindows ssize_t sys_recv_nt(struct Fd *fd, const struct iovec *iov,
                                size_t iovlen, uint32_t flags) {
  int err;
  ssize_t rc;
  uint32_t got;
  struct SockFd *sockfd;
  struct NtIovec iovnt[16];
  struct NtOverlapped overlapped = {.hEvent = WSACreateEvent()};
  err = errno;
  if (!WSARecv(fd->handle, iovnt, __iovec2nt(iovnt, iov, iovlen), 0, &flags,
               &overlapped, 0)) {
    if (WSAGetOverlappedResult(fd->handle, &overlapped, &got, false, &flags)) {
      rc = got;
    } else {
      rc = -1;
    }
  } else {
    errno = err;
    sockfd = (struct SockFd *)fd->extra;
    rc = __wsablock(fd, &overlapped, &flags, kSigOpRestartable,
                    sockfd->rcvtimeo);
  }
  unassert(WSACloseEvent(overlapped.hEvent));
  return rc;
}
