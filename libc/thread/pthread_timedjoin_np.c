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
#include "libc/assert.h"
#include "libc/calls/struct/timespec.h"
#include "libc/errno.h"
#include "libc/intrin/atomic.h"
#include "libc/intrin/dll.h"
#include "libc/thread/posixthread.internal.h"
#include "libc/thread/thread2.h"
#include "libc/thread/tls.h"
#include "libc/thread/wait0.internal.h"

/**
 * Waits for thread to terminate.
 *
 * Multiple threads joining the same thread is undefined behavior. If a
 * deferred or masked cancellation happens to the calling thread either
 * before or during the waiting process then the target thread will not
 * be joined. Calling pthread_join() on a non-joinable thread, e.g. one
 * that's been detached, is undefined behavior. If a thread attempts to
 * join itself, then the behavior is undefined.
 *
 * @param value_ptr if non-null will receive pthread_exit() argument
 *     if the thread called pthread_exit(), or `PTHREAD_CANCELED` if
 *     pthread_cancel() destroyed the thread instead
 * @param abstime specifies an absolute deadline or the timestamp of
 *     when we'll stop waiting; if this is null we will wait forever
 * @return 0 on success, or errno on error
 * @raise ECANCELED if calling thread was cancelled in masked mode
 * @raise EBUSY if `abstime` deadline elapsed
 * @cancellationpoint
 * @returnserrno
 * @threadsafe
 */
errno_t pthread_timedjoin_np(pthread_t thread, void **value_ptr,
                             struct timespec *abstime) {
  errno_t rc;
  struct PosixThread *pt;
  enum PosixThreadStatus status;
  pt = (struct PosixThread *)thread;
  status = atomic_load_explicit(&pt->status, memory_order_acquire);
  // "The behavior is undefined if the value specified by the thread
  //  argument to pthread_join() does not refer to a joinable thread."
  //                                  ──Quoth POSIX.1-2017
  unassert(status == kPosixThreadJoinable || status == kPosixThreadTerminated);
  if (!(rc = _wait0(&pt->tib->tib_tid, abstime))) {
    pthread_spin_lock(&_pthread_lock);
    dll_remove(&_pthread_list, &pt->list);
    pthread_spin_unlock(&_pthread_lock);
    if (value_ptr) {
      *value_ptr = pt->rc;
    }
    _pthread_free(pt);
    pthread_decimate_np();
  }
  return 0;
}
