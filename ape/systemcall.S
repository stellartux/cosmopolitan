/*-*- mode:unix-assembly; indent-tabs-mode:t; tab-width:8; coding:utf-8     -*-│
│vi: set et ft=asm ts=8 tw=8 fenc=utf-8                                     :vi│
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2023 Justine Alexandra Roberts Tunney                              │
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
#include "libc/macros.internal.h"

//	Invokes system call.
//
//	This function has eight parameters. The first seven are for
//	arguments passed along to the system call. The eight is for
//	the magic number that indicates which system call is called
//
//	The return value follows the Linux kernel convention, where
//	errors are returned as `-errno`. BSD systems are normalized
//	to follow this convention automatically.
SystemCall:
#ifdef __aarch64__
	mov	x8,x7
	mov	x9,0
	adds	x9,x9,0
	svc	0
	bcs	1f
	ret
1:	neg	x0,x0
	ret
#else
	mov	%rcx,%r10
	mov	16(%rsp),%eax
	clc
	syscall
	jnc	1f
	neg	%rax
1:	ret
#endif
	.endfn	SystemCall,globl
