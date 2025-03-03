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
#include "libc/calls/struct/fd.internal.h"
#include "libc/errno.h"
#include "libc/intrin/weaken.h"
#include "libc/nt/enum/filetype.h"
#include "libc/nt/files.h"
#include "libc/nt/runtime.h"
#include "libc/sysv/consts/o.h"

void sys_fcntl_nt_lock_cleanup(int);

textwindows int sys_close_nt(struct Fd *fd, int fildes) {
  int e;
  bool ok = true;

  if (_weaken(sys_fcntl_nt_lock_cleanup)) {
    _weaken(sys_fcntl_nt_lock_cleanup)(fildes);
  }

  if (fd->kind == kFdFile && ((fd->flags & O_ACCMODE) != O_RDONLY &&
                              GetFileType(fd->handle) == kNtFileTypeDisk)) {
    // Like Linux, closing a file on Windows doesn't guarantee it's
    // immediately synced to disk. But unlike Linux, this could cause
    // subsequent operations, e.g. unlink() to break w/ access error.
    e = errno;
    FlushFileBuffers(fd->handle);
    errno = e;
  }

  // if this file descriptor is wrapped in a named pipe worker thread
  // then we need to close our copy of the worker thread handle. it's
  // also required that whatever install a worker use malloc, so free
  if (!fd->dontclose) {
    if (!CloseHandle(fd->handle)) ok = false;
    if (fd->kind == kFdConsole && fd->extra && fd->extra != -1) {
      if (!CloseHandle(fd->extra)) ok = false;
    }
  }

  return ok ? 0 : -1;
}
