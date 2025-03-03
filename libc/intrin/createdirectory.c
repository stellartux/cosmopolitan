/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│vi: set net ft=c ts=2 sts=2 sw=2 fenc=utf-8                                :vi│
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2022 Justine Alexandra Roberts Tunney                              │
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
#include "libc/calls/syscall_support-nt.internal.h"
#include "libc/intrin/describeflags.internal.h"
#include "libc/intrin/strace.internal.h"
#include "libc/nt/files.h"
#include "libc/nt/thunk/msabi.h"

__msabi extern typeof(CreateDirectory) *const __imp_CreateDirectoryW;

/**
 * Creates directory on the New Technology.
 *
 * @return handle, or -1 on failure
 * @note this wrapper takes care of ABI, STRACE(), and __winerr()
 */
textwindows bool32 CreateDirectory(const char16_t *lpPathName,
                                   struct NtSecurityAttributes *lpSecurity) {
  bool32 ok;
  ok = __imp_CreateDirectoryW(lpPathName, lpSecurity);
  if (!ok) __winerr();
  NTTRACE("CreateDirectory(%#hs, %s) → %hhhd% m", lpPathName,
          DescribeNtSecurityAttributes(lpSecurity), ok);
  return ok;
}
