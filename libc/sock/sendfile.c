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
#include "libc/calls/calls.h"
#include "libc/calls/internal.h"
#include "libc/calls/sig.internal.h"
#include "libc/calls/syscall-sysv.internal.h"
#include "libc/calls/syscall_support-nt.internal.h"
#include "libc/dce.h"
#include "libc/errno.h"
#include "libc/intrin/asan.internal.h"
#include "libc/intrin/describeflags.internal.h"
#include "libc/intrin/safemacros.internal.h"
#include "libc/intrin/strace.internal.h"
#include "libc/macros.internal.h"
#include "libc/nt/enum/filetype.h"
#include "libc/nt/enum/wait.h"
#include "libc/nt/errors.h"
#include "libc/nt/files.h"
#include "libc/nt/struct/byhandlefileinformation.h"
#include "libc/nt/winsock.h"
#include "libc/sock/internal.h"
#include "libc/sock/sendfile.internal.h"
#include "libc/str/str.h"
#include "libc/sysv/errfuns.h"

// sendfile() isn't specified as raising eintr
static textwindows int SendfileBlock(int64_t handle,
                                     struct NtOverlapped *overlapped) {
  uint32_t i, got, flags = 0;
  if (WSAGetLastError() != kNtErrorIoPending &&
      WSAGetLastError() != WSAEINPROGRESS) {
    NTTRACE("TransmitFile failed %lm");
    return __winsockerr();
  }
  for (;;) {
    i = WSAWaitForMultipleEvents(1, &overlapped->hEvent, true,
                                 __SIG_POLLING_INTERVAL_MS, true);
    if (i == kNtWaitFailed) {
      NTTRACE("WSAWaitForMultipleEvents failed %lm");
      return __winsockerr();
    } else if (i == kNtWaitTimeout || i == kNtWaitIoCompletion) {
      if (_check_interrupts(kSigOpRestartable)) return -1;
#if _NTTRACE
      POLLTRACE("WSAWaitForMultipleEvents...");
#endif
    } else {
      break;
    }
  }
  if (WSAGetOverlappedResult(handle, overlapped, &got, false, &flags)) {
    return got;
  } else {
    return -1;
  }
}

static dontinline textwindows ssize_t sys_sendfile_nt(
    int outfd, int infd, int64_t *opt_in_out_inoffset, uint32_t uptobytes) {
  ssize_t rc;
  int64_t ih, oh, pos, eof, offset;
  struct NtByHandleFileInformation wst;
  if (!__isfdkind(infd, kFdFile)) return ebadf();
  if (!__isfdkind(outfd, kFdSocket)) return ebadf();
  ih = g_fds.p[infd].handle;
  oh = g_fds.p[outfd].handle;
  if (!SetFilePointerEx(ih, 0, &pos, SEEK_CUR)) {
    return __winerr();
  }
  if (opt_in_out_inoffset) {
    offset = *opt_in_out_inoffset;
  } else {
    offset = pos;
  }
  if (GetFileInformationByHandle(ih, &wst)) {
    // TransmitFile() returns EINVAL if `uptobytes` goes past EOF.
    eof = (uint64_t)wst.nFileSizeHigh << 32 | wst.nFileSizeLow;
    if (offset + uptobytes > eof) {
      uptobytes = eof - offset;
    }
  } else {
    return ebadf();
  }
  struct NtOverlapped ov = {
      .Pointer = (void *)(intptr_t)offset,
      .hEvent = WSACreateEvent(),
  };
  if (TransmitFile(oh, ih, uptobytes, 0, &ov, 0, 0)) {
    rc = uptobytes;
  } else {
    rc = SendfileBlock(oh, &ov);
  }
  if (rc != -1) {
    if (opt_in_out_inoffset) {
      *opt_in_out_inoffset = offset + rc;
      npassert(SetFilePointerEx(ih, pos, 0, SEEK_SET));
    } else {
      npassert(SetFilePointerEx(ih, offset + rc, 0, SEEK_SET));
    }
  }
  WSACloseEvent(ov.hEvent);
  return rc;
}

static ssize_t sys_sendfile_bsd(int outfd, int infd,
                                int64_t *opt_in_out_inoffset,
                                size_t uptobytes) {
  ssize_t rc;
  int64_t offset, sbytes;
  if (opt_in_out_inoffset) {
    offset = *opt_in_out_inoffset;
  } else if ((offset = lseek(infd, 0, SEEK_CUR)) == -1) {
    return -1;
  }
  if (IsFreebsd()) {
    rc = sys_sendfile_freebsd(infd, outfd, offset, uptobytes, 0, &sbytes, 0);
    if (rc == -1 && errno == ENOBUFS) errno = ENOMEM;
  } else {
    sbytes = uptobytes;
    rc = sys_sendfile_xnu(infd, outfd, offset, &sbytes, 0, 0);
  }
  if (rc == -1 && errno == ENOTSOCK) errno = EBADF;
  if (rc != -1) {
    if (opt_in_out_inoffset) {
      *opt_in_out_inoffset += sbytes;
    } else {
      npassert(lseek(infd, offset + sbytes, SEEK_SET) == offset + sbytes);
    }
    return sbytes;
  } else {
    return -1;
  }
}

/**
 * Transfers data from file to network.
 *
 * @param outfd needs to be a socket
 * @param infd needs to be a file
 * @param opt_in_out_inoffset may be specified for pread()-like behavior
 *     in which case the file position won't be changed; otherwise, this
 *     shall read from the file pointer which is advanced accordingly
 * @param uptobytes is the maximum number of bytes to send; some platforms
 *     block until everything's sent, whereas others won't; the behavior of
 *     zero is undefined; this value may overlap the end of file in which
 *     case what remains is sent; this is silently reduced to `0x7ffff000`
 * @return number of bytes transmitted which may be fewer than requested in
 *     which case caller must be prepared to call sendfile() again
 * @raise ESPIPE on Linux RHEL7+ if offset is used but `infd` isn't seekable,
 *     otherwise this could be EINVAL
 * @raise EPIPE on most systems if socket has been shutdown for reading or
 *     the remote end closed the connection, otherwise this could be EINVAL
 * @raise EBADF if `outfd` isn't a valid writeable stream sock descriptor
 * @raise EAGAIN if `O_NONBLOCK` is in play and it would have blocked
 * @raise EBADF if `infd` isn't a valid readable file descriptor
 * @raise EFAULT if `opt_in_out_inoffset` is a bad pointer
 * @raise EINVAL if `*opt_in_out_inoffset` is negative
 * @raise EOVERFLOW is documented as possible on Linux
 * @raise EIO if `infd` had a low-level i/o error
 * @raise ENOMEM if we require more vespene gas
 * @raise ENOTCONN if `outfd` isn't connected
 * @raise ENOSYS on NetBSD and OpenBSD
 * @see copy_file_range() for file ↔ file
 * @see splice() for fd ↔ pipe
 */
ssize_t sendfile(int outfd, int infd, int64_t *opt_in_out_inoffset,
                 size_t uptobytes) {
  ssize_t rc;

  // We must reduce this due to the uint32_t type conversion on Windows
  // which has a maximum of 0x7ffffffe. It also makes sendfile(..., -1)
  // less error prone, since Linux may EINVAL if greater than INT64_MAX
  uptobytes = MIN(uptobytes, 0x7ffff000);

  if (IsAsan() && opt_in_out_inoffset &&
      !__asan_is_valid(opt_in_out_inoffset, 8)) {
    rc = efault();
  } else if (IsLinux()) {
    rc = sys_sendfile(outfd, infd, opt_in_out_inoffset, uptobytes);
  } else if (IsFreebsd() || IsXnu()) {
    rc = sys_sendfile_bsd(outfd, infd, opt_in_out_inoffset, uptobytes);
  } else if (IsWindows()) {
    rc = sys_sendfile_nt(outfd, infd, opt_in_out_inoffset, uptobytes);
  } else {
    rc = enosys();
  }

  STRACE("sendfile(%d, %d, %p, %'zu) → %ld% m", outfd, infd,
         DescribeInOutInt64(rc, opt_in_out_inoffset), uptobytes, rc);
  return rc;
}
