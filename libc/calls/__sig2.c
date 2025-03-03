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
#include "ape/sections.internal.h"
#include "libc/assert.h"
#include "libc/calls/calls.h"
#include "libc/calls/internal.h"
#include "libc/calls/sig.internal.h"
#include "libc/calls/state.internal.h"
#include "libc/calls/struct/sigaction.h"
#include "libc/calls/struct/siginfo.h"
#include "libc/calls/struct/sigset.h"
#include "libc/intrin/strace.internal.h"
#include "libc/intrin/weaken.h"
#include "libc/macros.internal.h"
#include "libc/nt/runtime.h"
#include "libc/runtime/internal.h"
#include "libc/runtime/runtime.h"
#include "libc/str/str.h"
#include "libc/sysv/consts/sa.h"
#include "libc/sysv/consts/sig.h"
#include "libc/sysv/errfuns.h"
#include "libc/thread/tls.h"

#ifdef __x86_64__

/**
 * Allocates piece of memory for storing pending signal.
 * @assume lock is held
 */
static textwindows struct Signal *__sig_alloc(void) {
  int i;
  struct Signal *res = 0;
  for (i = 0; i < ARRAYLEN(__sig.mem); ++i) {
    if (!__sig.mem[i].used) {
      __sig.mem[i].used = true;
      res = __sig.mem + i;
      break;
    }
  }
  return res;
}

/**
 * Returns signal memory to static pool.
 */
static textwindows void __sig_free(struct Signal *mem) {
  mem->used = false;
}

static inline textwindows int __sig_is_masked(int sig) {
  if (__tls_enabled) {
    return __get_tls()->tib_sigmask & (1ull << (sig - 1));
  } else {
    return __sig.sigmask & (1ull << (sig - 1));
  }
}

textwindows int __sig_is_applicable(struct Signal *s) {
  return s->tid <= 0 || s->tid == gettid();
}

/**
 * Dequeues signal that isn't masked.
 * @return signal or null if empty or none unmasked
 */
static textwindows struct Signal *__sig_remove(int sigops) {
  struct Signal *prev, *res;
  if (__sig.queue) {
    __sig_lock();
    for (prev = 0, res = __sig.queue; res; prev = res, res = res->next) {
      if (__sig_is_applicable(res) &&    //
          !__sig_is_masked(res->sig) &&  //
          !((sigops & kSigOpNochld) && res->sig == SIGCHLD)) {
        if (res == __sig.queue) {
          __sig.queue = res->next;
        } else if (prev) {
          prev->next = res->next;
        }
        res->next = 0;
        break;
      }
    }
    __sig_unlock();
  } else {
    res = 0;
  }
  return res;
}

/**
 * Delivers signal to callback.
 * @note called from main thread
 * @return true if EINTR should be returned by caller
 */
static bool __sig_deliver(int sigops, int sig, int si_code, ucontext_t *ctx) {
  unsigned rva, flags;
  siginfo_t info, *infop;
  STRACE("delivering %G", sig);

  // enter the signal
  rva = __sighandrvas[sig];
  flags = __sighandflags[sig];
  if ((~flags & SA_NODEFER) || (flags & SA_RESETHAND)) {
    // by default we try to avoid reentering a signal handler. for
    // example, if a sigsegv handler segfaults, then we'd want the
    // second signal to just kill the process. doing this means we
    // track state. that's bad if you want to longjmp() out of the
    // signal handler. in that case you must use SA_NODEFER.
    __sighandrvas[sig] = (int32_t)(intptr_t)SIG_DFL;
  }

  // setup the somewhat expensive information args
  // only if they're requested by the user in sigaction()
  if (flags & SA_SIGINFO) {
    bzero(&info, sizeof(info));
    info.si_signo = sig;
    info.si_code = si_code;
    infop = &info;
  } else {
    infop = 0;
    ctx = 0;
  }

  // handover control to user
  ((sigaction_f)(__executable_start + rva))(sig, infop, ctx);

  if ((~flags & SA_NODEFER) && (~flags & SA_RESETHAND)) {
    // it's now safe to reenter the signal so we need to restore it.
    // since sigaction() is @asyncsignalsafe we only restore it if the
    // user didn't change it during the signal handler. we also don't
    // need to do anything if this was a oneshot signal or nodefer.
    if (__sighandrvas[sig] == (int32_t)(intptr_t)SIG_DFL) {
      __sighandrvas[sig] = rva;
    }
  }

  if (!(sigops & kSigOpRestartable)) {
    return true;  // always send EINTR for wait4(), poll(), etc.
  } else if (flags & SA_RESTART) {
    STRACE("restarting syscall on %G", sig);
    return false;  // resume syscall for read(), write(), etc.
  } else {
    return true;  // default course is to raise EINTR
  }
}

/**
 * Returns true if signal default action is to end process.
 */
static textwindows bool __sig_is_fatal(int sig) {
  if (sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH) {
    return false;
  } else {
    return true;
  }
}

/**
 * Handles signal.
 * @return true if signal was delivered
 */
textwindows bool __sig_handle(int sigops, int sig, int si_code,
                              ucontext_t *ctx) {
  bool delivered;
  switch (__sighandrvas[sig]) {
    case (intptr_t)SIG_DFL:
      if (__sig_is_fatal(sig)) {
        intptr_t hStderr;
        const char *signame;
        char *end, sigbuf[21], output[22];
        signame = strsignal_r(sig, sigbuf);
        STRACE("terminating due to uncaught %s", signame);
        hStderr = GetStdHandle(kNtStdErrorHandle);
        end = stpcpy(stpcpy(output, signame), "\n");
        WriteFile(hStderr, output, end - output, 0, 0);
        if (_weaken(__restore_console_win32)) {
          _weaken(__restore_console_win32)();
        }
        ExitProcess(sig);
      }
      // fallthrough
    case (intptr_t)SIG_IGN:
      STRACE("ignoring %G", sig);
      delivered = false;
      break;
    default:
      delivered = __sig_deliver(sigops, sig, si_code, ctx);
      break;
  }
  return delivered;
}

/**
 * Handles signal immediately if not blocked.
 *
 * @param restartable is for functions like read() but not poll()
 * @return true if EINTR should be returned by caller
 * @return 1 if delivered, 0 if enqueued, otherwise -1 w/ errno
 * @note called from main thread
 * @threadsafe
 */
textwindows int __sig_raise(int sig, int si_code) {
  if (1 <= sig && sig <= 64) {
    if (!__sig_is_masked(sig)) {
      ++__sig_count;
      // TODO(jart): ucontext_t support
      __sig_handle(false, sig, si_code, 0);
      return 0;
    } else {
      STRACE("%G is masked", sig);
      return __sig_add(gettid(), sig, si_code);
    }
  } else {
    return einval();
  }
}

/**
 * Enqueues generic signal for delivery on New Technology.
 * @return 0 on success, otherwise -1 w/ errno
 * @threadsafe
 */
textwindows int __sig_add(int tid, int sig, int si_code) {
  int rc;
  struct Signal *mem;
  if (1 <= sig && sig <= 64) {
    if (__sighandrvas[sig] == (unsigned)(uintptr_t)SIG_IGN ||
        (__sighandrvas[sig] == (unsigned)(uintptr_t)SIG_DFL &&
         !__sig_is_fatal(sig))) {
      STRACE("ignoring %G", sig);
      rc = 0;
    } else {
      STRACE("enqueuing %G", sig);
      __sig_lock();
      ++__sig_count;
      if ((mem = __sig_alloc())) {
        mem->tid = tid;
        mem->sig = sig;
        mem->si_code = si_code;
        mem->next = __sig.queue;
        __sig.queue = mem;
        rc = 0;
      } else {
        rc = enomem();
      }
      __sig_unlock();
    }
  } else {
    rc = einval();
  }
  return rc;
}

/**
 * Checks for unblocked signals and delivers them on New Technology.
 * @return true if EINTR should be returned by caller
 * @note called from main thread
 * @threadsafe
 */
textwindows bool __sig_check(int sigops) {
  bool delivered;
  struct Signal *sig;
  delivered = false;
  while ((sig = __sig_remove(sigops))) {
    delivered |= __sig_handle(sigops, sig->sig, sig->si_code, 0);
    __sig_free(sig);
  }
  return delivered;
}

/**
 * Determines if a signal should be ignored and if so discards existing
 * instances of said signal on New Technology.
 *
 * Even blocked signals are discarded.
 *
 * @param sig the signal number to remove
 * @threadsafe
 */
textwindows void __sig_check_ignore(const int sig, const unsigned rva) {
  struct Signal *cur, *prev, *next;
  if (rva != (unsigned)(intptr_t)SIG_IGN &&
      (rva != (unsigned)(intptr_t)SIG_DFL || __sig_is_fatal(sig))) {
    return;
  }
  if (__sig.queue) {
    __sig_lock();
    for (prev = 0, cur = __sig.queue; cur; cur = next) {
      next = cur->next;
      if (sig == cur->sig) {
        if (cur == __sig.queue) {
          __sig.queue = cur->next;
        } else if (prev) {
          prev->next = cur->next;
        }
        __sig_handle(false, cur->sig, cur->si_code, 0);
        __sig_free(cur);
      } else {
        prev = cur;
      }
    }
    __sig_unlock();
  }
}

#endif /* __x86_64__ */
