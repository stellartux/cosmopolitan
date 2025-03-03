#ifndef COSMOPOLITAN_LIBC_CALLS_SYSCALL_SUPPORT_SYSV_INTERNAL_H_
#define COSMOPOLITAN_LIBC_CALLS_SYSCALL_SUPPORT_SYSV_INTERNAL_H_
#if !(__ASSEMBLER__ + __LINKER__ + 0)
COSMOPOLITAN_C_START_
/*───────────────────────────────────────────────────────────────────────────│─╗
│ cosmopolitan § syscalls » system five » structless support               ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

bool __is_linux_2_6_23(void);
bool32 sys_isatty_metal(int);
int __fixupnewfd(int, int);
int __notziposat(int, const char *);
int __tkill(int, int, void *);
int _fork(uint32_t);
int _isptmaster(int);
int _ptsname(int, char *, size_t);
int getdomainname_linux(char *, size_t);
int gethostname_bsd(char *, size_t, int);
int gethostname_linux(char *, size_t);
int gethostname_nt(char *, size_t, int);
int sys_msyscall(void *, size_t);
long sys_bogus(void);
ssize_t __getrandom(void *, size_t, unsigned);
void *__vdsosym(const char *, const char *);
void __onfork(void);
void __restore_rt();
void __restore_rt_netbsd(void);
void cosmo2flock(uintptr_t);
void flock2cosmo(uintptr_t);

COSMOPOLITAN_C_END_
#endif /* !(__ASSEMBLER__ + __LINKER__ + 0) */
#endif /* COSMOPOLITAN_LIBC_CALLS_SYSCALL_SUPPORT_SYSV_INTERNAL_H_ */
