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
#include "libc/dce.h"
#include "libc/intrin/asan.internal.h"
#include "libc/str/str.h"
#ifndef __aarch64__

/**
 * Compares NUL-terminated strings.
 *
 * @param a is first non-null NUL-terminated string pointer
 * @param b is second non-null NUL-terminated string pointer
 * @return is <0, 0, or >0 based on uint8_t comparison
 * @asyncsignalsafe
 */
dontasan int strcmp(const char *a, const char *b) {
  int c;
  size_t i = 0;
  uint64_t v, w;
  if (a == b) return 0;
  if (IsAsan()) __asan_verify_str(a);
  if (IsAsan()) __asan_verify_str(b);
  if ((c = (*a & 255) - (*b & 255))) return c;
  if (!IsTiny() && ((uintptr_t)a & 7) == ((uintptr_t)b & 7)) {
    for (; (uintptr_t)(a + i) & 7; ++i) {
      if (a[i] != b[i] || !b[i]) {
        return (a[i] & 255) - (b[i] & 255);
      }
    }
    for (;; i += 8) {
      v = *(uint64_t *)(a + i);
      w = *(uint64_t *)(b + i);
      w = (v ^ w) | (~v & (v - 0x0101010101010101) & 0x8080808080808080);
      if (w) {
        i += (unsigned)__builtin_ctzll(w) >> 3;
        break;
      }
    }
  } else {
    while (a[i] == b[i] && b[i]) ++i;
  }
  return (a[i] & 255) - (b[i] & 255);
}

#endif /* __aarch64__ */
