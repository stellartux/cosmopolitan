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
#include "libc/calls/calls.h"
#include "libc/calls/sig.internal.h"
#include "libc/calls/syscall-sysv.internal.h"
#include "libc/dce.h"
#include "libc/intrin/strace.internal.h"
#include "libc/intrin/weaken.h"
#include "libc/nt/runtime.h"
#include "libc/runtime/internal.h"
#include "libc/sysv/consts/sicode.h"
#include "libc/sysv/consts/sig.h"
#include "libc/thread/tls.h"
#include "libc/thread/xnu.internal.h"

static dontubsan void RaiseSigFpe(void) {
  volatile int x = 0;
  x = 1 / x;
}

/**
 * Sends signal to self.
 *
 * This is basically the same as:
 *
 *     tkill(gettid(), sig);
 *
 * Note `SIG_DFL` still results in process death for most signals.
 *
 * This function is not entirely equivalent to kill() or tkill(). For
 * example, we raise `SIGTRAP` and `SIGFPE` the natural way, since that
 * helps us support Windows. So if the raised signal has a signal
 * handler, then the reported `si_code` might not be `SI_TKILL`.
 *
 * @param sig can be SIGALRM, SIGINT, SIGTERM, SIGKILL, etc.
 * @return 0 if signal was delivered and returned, or -1 w/ errno
 * @asyncsignalsafe
 */
int raise(int sig) {
  int rc;
  STRACE("raise(%G) → ...", sig);
  if (sig == SIGTRAP) {
    DebugBreak();
    rc = 0;
#ifndef __aarch64__
  } else if (sig == SIGFPE) {
    // TODO(jart): Why doesn't AARCH64 raise SIGFPE?
    RaiseSigFpe();
    rc = 0;
#endif
  } else if (IsLinux() || IsXnu() || IsFreebsd() || IsOpenbsd() || IsNetbsd()) {
    rc = sys_tkill(gettid(), sig, 0);
  } else if (IsWindows() || IsMetal()) {
    if (IsWindows() && sig == SIGKILL) {
      // TODO(jart): Isn't this implemented by __sig_raise()?
      if (_weaken(__restore_console_win32)) {
        _weaken(__restore_console_win32)();
      }
      ExitProcess(sig);
    } else {
      rc = __sig_raise(sig, SI_TKILL);
    }
  } else {
    __builtin_unreachable();
  }
  STRACE("...raise(%G) → %d% m", sig, rc);
  return rc;
}
