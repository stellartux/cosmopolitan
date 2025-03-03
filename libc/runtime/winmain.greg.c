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
#include "libc/calls/state.internal.h"
#include "libc/calls/syscall_support-nt.internal.h"
#include "libc/dce.h"
#include "libc/intrin/describeflags.internal.h"
#include "libc/intrin/getenv.internal.h"
#include "libc/intrin/weaken.h"
#include "libc/log/libfatal.internal.h"
#include "libc/macros.internal.h"
#include "libc/nexgen32e/rdtsc.h"
#include "libc/nt/console.h"
#include "libc/nt/enum/accessmask.h"
#include "libc/nt/enum/consolemodeflags.h"
#include "libc/nt/enum/creationdisposition.h"
#include "libc/nt/enum/fileflagandattributes.h"
#include "libc/nt/enum/filemapflags.h"
#include "libc/nt/enum/filesharemode.h"
#include "libc/nt/enum/pageflags.h"
#include "libc/nt/files.h"
#include "libc/nt/ipc.h"
#include "libc/nt/memory.h"
#include "libc/nt/pedef.internal.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/signals.h"
#include "libc/nt/struct/ntexceptionpointers.h"
#include "libc/nt/struct/teb.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/thread.h"
#include "libc/nt/thunk/msabi.h"
#include "libc/runtime/internal.h"
#include "libc/runtime/memtrack.internal.h"
#include "libc/runtime/runtime.h"
#include "libc/runtime/stack.h"
#include "libc/runtime/winargs.internal.h"
#include "libc/sock/internal.h"
#include "libc/sysv/consts/prot.h"

#ifdef __x86_64__

// clang-format off
__msabi extern typeof(CreateFileMapping) *const __imp_CreateFileMappingW;
__msabi extern typeof(DuplicateHandle) *const __imp_DuplicateHandle;
__msabi extern typeof(ExitProcess) *const __imp_ExitProcess;
__msabi extern typeof(FreeEnvironmentStrings) *const __imp_FreeEnvironmentStringsW;
__msabi extern typeof(GetConsoleMode) *const __imp_GetConsoleMode;
__msabi extern typeof(GetCurrentProcess) *const __imp_GetCurrentProcess;
__msabi extern typeof(GetCurrentProcessId) *const __imp_GetCurrentProcessId;
__msabi extern typeof(GetEnvironmentStrings) *const __imp_GetEnvironmentStringsW;
__msabi extern typeof(GetStdHandle) *const __imp_GetStdHandle;
__msabi extern typeof(MapViewOfFileEx) *const __imp_MapViewOfFileEx;
__msabi extern typeof(SetConsoleCP) *const __imp_SetConsoleCP;
__msabi extern typeof(SetConsoleMode) *const __imp_SetConsoleMode;
__msabi extern typeof(SetConsoleOutputCP) *const __imp_SetConsoleOutputCP;
__msabi extern typeof(SetStdHandle) *const __imp_SetStdHandle;
__msabi extern typeof(VirtualProtect) *const __imp_VirtualProtect;
__msabi extern typeof(WriteFile) *const __imp_WriteFile;
// clang-format on

extern void cosmo(int, char **, char **, long (*)[2]) wontreturn;

static const signed char kNtStdio[3] = {
    (signed char)kNtStdInputHandle,
    (signed char)kNtStdOutputHandle,
    (signed char)kNtStdErrorHandle,
};

static const short kConsoleModes[3] = {
    kNtEnableProcessedInput | kNtEnableLineInput | kNtEnableEchoInput |
        kNtEnableMouseInput | kNtEnableQuickEditMode | kNtEnableExtendedFlags |
        kNtEnableAutoPosition | kNtEnableInsertMode |
        kNtEnableVirtualTerminalInput,
    kNtEnableProcessedOutput | kNtEnableWrapAtEolOutput |
        kNtEnableVirtualTerminalProcessing,
    kNtEnableProcessedOutput | kNtEnableWrapAtEolOutput |
        kNtEnableVirtualTerminalProcessing,
};

static uint32_t __init_pid;
static uint32_t __console_mode[3];

// implements all win32 apis on non-windows hosts
__msabi long __oops_win32(void) {
  assert(!"win32 api called on non-windows host");
  return 0;
}

// called by _exit to undo our config changes to cmd.exe
// it must never ever be called from forked subprocesses
void __restore_console_win32(void) {
  if (__imp_GetCurrentProcessId() == __init_pid) {
    for (int i = 0; i < 3; ++i) {
      __imp_SetConsoleMode(__imp_GetStdHandle(kNtStdio[i]), __console_mode[i]);
    }
  }
}

// https://nullprogram.com/blog/2022/02/18/
__msabi static inline char16_t *MyCommandLine(void) {
  void *cmd;
  asm("mov\t%%gs:(0x60),%0\n"
      "mov\t0x20(%0),%0\n"
      "mov\t0x78(%0),%0\n"
      : "=r"(cmd));
  return cmd;
}

// this ensures close(1) won't accidentally close(2) for example
__msabi static textwindows void DeduplicateStdioHandles(void) {
  for (long i = 0; i < 3; ++i) {
    int64_t h1 = __imp_GetStdHandle(kNtStdio[i]);
    for (long j = i + 1; j < 3; ++j) {
      int64_t h2 = __imp_GetStdHandle(kNtStdio[j]);
      if (h1 == h2) {
        int64_t h3, proc = __imp_GetCurrentProcess();
        __imp_DuplicateHandle(proc, h2, proc, &h3, 0, true,
                              kNtDuplicateSameAccess);
        __imp_SetStdHandle(kNtStdio[j], h3);
      }
    }
  }
}

__msabi static textwindows wontreturn void WinMainNew(const char16_t *cmdline) {
  size_t stacksize;
  struct WinArgs *wa;
  uintptr_t stackaddr;
  __init_pid = __pid;
  __oldstack = (intptr_t)__builtin_frame_address(0);
  if (NtGetPeb()->OSMajorVersion >= 10 &&
      (intptr_t)v_ntsubsystem == kNtImageSubsystemWindowsCui) {
    __imp_SetConsoleCP(kNtCpUtf8);
    __imp_SetConsoleOutputCP(kNtCpUtf8);
    for (int i = 0; i < 3; ++i) {
      int64_t hand = __imp_GetStdHandle(kNtStdio[i]);
      __imp_GetConsoleMode(hand, __console_mode + i);
      __imp_SetConsoleMode(hand, kConsoleModes[i]);
    }
  }
  _Static_assert(sizeof(struct WinArgs) % FRAMESIZE == 0, "");
  _mmi.p = _mmi.s;
  _mmi.n = ARRAYLEN(_mmi.s);
  stackaddr = GetStaticStackAddr(0);
  stacksize = GetStaticStackSize();
  __imp_MapViewOfFileEx((_mmi.p[0].h = __imp_CreateFileMappingW(
                             -1, &kNtIsInheritable, kNtPageExecuteReadwrite,
                             stacksize >> 32, stacksize, NULL)),
                        kNtFileMapWrite | kNtFileMapExecute, 0, 0, stacksize,
                        (void *)stackaddr);
  int prot = (intptr_t)ape_stack_prot;
  if (~prot & PROT_EXEC) {
    uint32_t old;
    __imp_VirtualProtect((void *)stackaddr, stacksize, kNtPageReadwrite, &old);
  }
  _mmi.p[0].x = stackaddr >> 16;
  _mmi.p[0].y = (stackaddr >> 16) + ((stacksize - 1) >> 16);
  _mmi.p[0].prot = prot;
  _mmi.p[0].flags = 0x00000026;  // stack+anonymous
  _mmi.p[0].size = stacksize;
  _mmi.i = 1;
  wa = (struct WinArgs *)(stackaddr + (stacksize - sizeof(struct WinArgs)));
  int count = GetDosArgv(cmdline, wa->argblock, ARRAYLEN(wa->argblock),
                         wa->argv, ARRAYLEN(wa->argv));
  for (int i = 0; wa->argv[0][i]; ++i) {
    if (wa->argv[0][i] == '\\') {
      wa->argv[0][i] = '/';
    }
  }
  char16_t *env16 = __imp_GetEnvironmentStringsW();
  GetDosEnviron(env16, wa->envblock, ARRAYLEN(wa->envblock) - 8, wa->envp,
                ARRAYLEN(wa->envp) - 1);
  __imp_FreeEnvironmentStringsW(env16);
  __envp = &wa->envp[0];
  _jmpstack((char *)(stackaddr + (stacksize - sizeof(struct WinArgs))), cosmo,
            count, wa->argv, wa->envp, wa->auxv);
}

__msabi textwindows int64_t WinMain(int64_t hInstance, int64_t hPrevInstance,
                                    const char *lpCmdLine, int64_t nCmdShow) {
  const char16_t *cmdline;
  extern char os asm("__hostos");
  os = _HOSTWINDOWS;  // madness https://news.ycombinator.com/item?id=21019722
  kStartTsc = rdtsc();
  __umask = 077;
  __pid = __imp_GetCurrentProcessId();
  cmdline = MyCommandLine();
#ifdef SYSDEBUG
  // sloppy flag-only check for early initialization
  if (__strstr16(cmdline, u"--strace")) ++__strace;
#endif
  DeduplicateStdioHandles();
  if (_weaken(WinMainStdin)) {
    _weaken(WinMainStdin)();
  }
  if (_weaken(WinSockInit)) {
    _weaken(WinSockInit)();
  }
  if (_weaken(WinMainForked)) {
    _weaken(WinMainForked)();
  }
  WinMainNew(cmdline);
}

#endif /* __x86_64__ */
