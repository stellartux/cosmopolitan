#-*-mode:makefile-gmake;indent-tabs-mode:t;tab-width:8;coding:utf-8-*-┐
#───vi: set et ft=make ts=8 tw=8 fenc=utf-8 :vi───────────────────────┘
#
# SYNOPSIS
#
#   Cosmopolitan Core Build Definitions
#
# DESCRIPTION
#
#   Nearly all compiler flag tuning is done within this one file. Flags
#   may be customized with environment variables. We also use this file
#   to compute expensive values once per build.
#
#   When tuning the variables below, please note they're interpreted in
#   the strictest sense. For example, we don't pass CFLAGS to gcc if we
#   know it's compiling a .S file.  This enables our `make V=0` logging
#   to be succinct and informative at the cost of being less forgiving.
#
#   Further note that link order is equally unforgiving in repositories
#   of this scale. We approach that by over-specifying dependencies, in
#   packages that're broken down usually on a per-directory basis. This
#   is aided by the checkdeps and functional programming tools which in
#   most cases should be able to deduce correct ordering automatically.
#
#   Concerning our approach to flag tuning, most of it is non-essential
#   and largely serves to turn features off. Particularly features that
#   would otherwise lock us in to a particular platform or legal terms.
#   Other flags will usually be the ones that provide us marginal gains
#   in terms of performance and code size, but the world won't end when
#   they aren't used. Flags that play a critical role in source working
#   correctly will usually be specified on a object-by-object basis, in
#   their respective packages.
#
# VARIABLES
#
#   Our configuration variables, ordered by increasing preference:
#
#     CCFLAGS      frontend flags (.i, .c, .cc, .f, .S, .lds, etc.)
#     OFLAGS       objectify flags (precludes -S and -E)
#     CPPFLAGS     preprocessor flags (.h, .c, .cc, .S, .inc, .lds, etc.)
#     TARGET_ARCH  microarchitecture flags (e.g. -march=native)
#     COPTS        c/c++ flags (.c, .cc)
#     CFLAGS       c flags (.c only)
#     CXXFLAGS     c++ flags (.cc only)
#     LDFLAGS      linker flags (don't use -Wl, frontend prefix)
#     ASFLAGS      assembler flags (don't use -Wa, frontend prefix)
#
#   For each FOO above, there exists (by increasing preference)
#
#     DEFAULT_FOO  see build/definitions.mk
#     CONFIG_FOO   see build/config.mk
#     FOO          set ~/.cosmo.mk and target-specific
#     OVERRIDE_FOO set ~/.cosmo.mk and target-specific (use rarely)
#

LC_ALL = C
SOURCE_DATE_EPOCH = 0

ARFLAGS = rcsD
ZFLAGS ?=
XARGS ?= xargs -P4 -rs8000
DOT ?= dot
CLANG = clang
FC = gfortran  #/opt/cross9f/bin/x86_64-linux-musl-gfortran
TMPDIR = o/tmp

AR = build/bootstrap/ar.com
CP = build/bootstrap/cp.com
RM = build/bootstrap/rm.com -f
GZIP = build/bootstrap/gzip.com
ECHO = build/bootstrap/echo.com
CHMOD = build/bootstrap/chmod.com
TOUCH = build/bootstrap/touch.com
PKG = build/bootstrap/package.com
MKDEPS = build/bootstrap/mkdeps.com
ZIPOBJ = build/bootstrap/zipobj.com
ZIPCOPY = build/bootstrap/zipcopy.com
PECHECK = build/bootstrap/pecheck.com
FIXUPOBJ = build/bootstrap/fixupobj.com
MKDIR = build/bootstrap/mkdir.com -p
COMPILE = build/bootstrap/compile.com -V9 -P4096 $(QUOTA)

COMMA := ,
PWD := $(shell build/bootstrap/pwd.com)

IGNORE := $(shell $(MKDIR) $(TMPDIR))

ifneq ($(findstring aarch64,$(MODE)),)
ARCH = aarch64
VM = o/third_party/qemu/qemu-aarch64
HOSTS ?= pi silicon
else
ARCH = x86_64
HOSTS ?= freebsd openbsd netbsd rhel7 rhel5 xnu win10
endif

ifeq ($(PREFIX),)
ifeq ($(USE_SYSTEM_TOOLCHAIN),)
ifneq ("$(wildcard o/third_party/gcc/bin/x86_64-pc-linux-gnu-*)","")
PREFIX = o/third_party/gcc/bin/x86_64-pc-linux-gnu-
else
IGNORE := $(shell build/bootstrap/unbundle.com)
PREFIX = o/third_party/gcc/bin/x86_64-linux-musl-
endif
ifeq ($(ARCH), aarch64)
PREFIX = o/third_party/gcc/bin/aarch64-linux-musl-
endif
endif
endif

AS = $(PREFIX)as
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
CXXFILT = $(PREFIX)c++filt
LD = $(PREFIX)ld.bfd
NM = $(PREFIX)nm
GCC = $(PREFIX)gcc
STRIP = $(PREFIX)strip
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
ifneq ($(wildcard $(PWD)/$(PREFIX)addr2line), )
ADDR2LINE = $(PWD)/$(PREFIX)addr2line
else
ADDR2LINE = $(PREFIX)addr2line
endif

export ADDR2LINE
export LC_ALL
export MKDIR
export MODE
export SOURCE_DATE_EPOCH
export TMPDIR

ifeq ($(LANDLOCKMAKE_VERSION),)
TMPSAFE = $(join $(TMPDIR),$(subst /,_,$@)).tmp
else
TMPSAFE = $(TMPDIR)/
endif

ifeq ($(ARCH), aarch64)
IMAGE_BASE_VIRTUAL ?= 0x010000000000
else
IMAGE_BASE_VIRTUAL ?= 0x400000
endif

BACKTRACES =								\
	-fno-optimize-sibling-calls					\
	-mno-omit-leaf-frame-pointer

ifneq ($(ARCH), aarch64)
BACKTRACES += -fno-schedule-insns2
endif

SANITIZER =								\
	-fsanitize=address

NO_MAGIC =								\
	-fno-stack-protector						\
	-fwrapv								\
	-fno-sanitize=all

OLD_CODE =								\
	-fno-strict-aliasing						\
	-fno-strict-overflow

TRADITIONAL =								\
	-Wno-implicit-int						\
	-Wno-return-type						\
	-Wno-pointer-sign

DEFAULT_CCFLAGS +=							\
	-Wall								\
	-Werror								\
	-fno-omit-frame-pointer						\
	-frecord-gcc-switches

DEFAULT_COPTS ?=							\
	-fno-math-errno							\
	-fno-ident							\
	-fno-common							\
	-fno-gnu-unique							\
	-fstrict-aliasing						\
	-fstrict-overflow						\
	-fno-semantic-interposition					\
	-fno-dwarf2-cfi-asm						\
	-fno-unwind-tables						\
	-fno-asynchronous-unwind-tables

ifeq ($(ARCH), x86_64)
# Microsoft says "[a]ny memory below the stack beyond the red zone
# [note: Windows defines the x64 red zone size as 0] is considered
# volatile and may be modified by the operating system at any time."
# https://devblogs.microsoft.com/oldnewthing/20190111-00/?p=100685
DEFAULT_COPTS +=							\
	-mno-red-zone							\
	-mno-tls-direct-seg-refs
endif

ifeq ($(ARCH), aarch64)
#
# - Apple says in "Writing ARM64 code for Apple platforms" that we're
#   not allowed to use the x18 register.
#
# - Cosmopolitan Libc uses x28 for thread-local storage because Apple
#   forbids us from using tpidr_el0 too.
#
# - Cosmopolitan currently lacks an implementation of the runtime
#   libraries needed by the -moutline-atomics flag
#
DEFAULT_COPTS +=							\
	-ffixed-x18							\
	-ffixed-x28							\
	-mno-outline-atomics
endif

MATHEMATICAL =								\
	-O3								\
	-fwrapv

DEFAULT_CPPFLAGS +=							\
	-D_COSMO_SOURCE							\
	-DMODE='"$(MODE)"'						\
	-DIMAGE_BASE_VIRTUAL=$(IMAGE_BASE_VIRTUAL)			\
	-nostdinc							\
	-iquote .

DEFAULT_CFLAGS =							\
	-std=gnu2x

DEFAULT_CXXFLAGS =							\
	-fno-rtti							\
	-fno-exceptions							\
	-fuse-cxa-atexit						\
	-Wno-int-in-bool-context					\
	-Wno-narrowing							\
	-Wno-literal-suffix

DEFAULT_ASFLAGS =							\
	-W								\
	-I.								\
	--noexecstack

DEFAULT_LDFLAGS =							\
	-static								\
	-nostdlib							\
	-znorelro							\
	--gc-sections							\
	--build-id=none							\
	--no-dynamic-linker

# # generate linker report files
# DEFAULT_LDFLAGS += --cref -Map=$@.map

ifeq ($(ARCH), aarch64)
DEFAULT_LDFLAGS +=							\
	-zmax-page-size=0x4000						\
	-zcommon-page-size=0x4000					\
	-znorelro
else
DEFAULT_LDFLAGS +=							\
	-zmax-page-size=0x4000						\
	-zcommon-page-size=0x1000
endif

ASONLYFLAGS =								\
	-c								\
	-g

DEFAULT_LDLIBS =

MCA =	llvm-mca-10							\
	-mtriple=x86_64-pc-linux-gnu					\
	-instruction-info						\
	-iterations=3							\
	-all-stats							\
	-all-views							\
	-timeline

cc.flags =								\
	$(DEFAULT_CCFLAGS)						\
	$(CONFIG_CCFLAGS)						\
	$(CCFLAGS)							\
	$(OVERRIDE_CCFLAGS)

o.flags =								\
	$(DEFAULT_OFLAGS)						\
	$(CONFIG_OFLAGS)						\
	$(OFLAGS)							\
	$(OVERRIDE_OFLAGS)

cpp.flags =								\
	$(DEFAULT_CPPFLAGS)						\
	$(CONFIG_CPPFLAGS)						\
	$(CPPFLAGS)							\
	$(OVERRIDE_CPPFLAGS)						\
	-include libc/integral/normalize.inc

copt.flags =								\
	$(TARGET_ARCH)							\
	$(DEFAULT_COPTS)						\
	$(CONFIG_COPTS)							\
	$(COPTS)							\
	$(OVERRIDE_COPTS)

f.flags =								\
	$(DEFAULT_FFLAGS)						\
	$(CONFIG_FFLAGS)						\
	$(FFLAGS)							\
	$(OVERRIDE_FFLAGS)

c.flags =								\
	$(DEFAULT_CFLAGS)						\
	$(CONFIG_CFLAGS)						\
	$(CFLAGS)							\
	$(OVERRIDE_CFLAGS)

cxx.flags =								\
	$(DEFAULT_CXXFLAGS)						\
	$(CONFIG_CXXFLAGS)						\
	$(CXXFLAGS)							\
	$(OVERRIDE_CXXFLAGS)

s.flags =								\
	$(DEFAULT_ASFLAGS)						\
	$(CONFIG_ASFLAGS)						\
	$(ASFLAGS)							\
	$(OVERRIDE_ASFLAGS)

S.flags = $(addprefix -Wa$(COMMA),$(s.flags))

LD.libs =								\
	$(LDLIBS)							\
	$(LOADLIBES)							\
	$(DEFAULT_LDLIBS)						\
	$(CONFIG_LDLIBS)						\
	$(LDLIBS)							\
	$(DEFAULT_LIBS)							\
	$(CONFIG_LIBS)							\
	$(LIBS)

COMPILE.c.flags = $(cc.flags) $(copt.flags) $(cpp.flags) $(c.flags)
COMPILE.cxx.flags = $(cc.flags) $(copt.flags) $(cpp.flags) $(cxx.flags)
COMPILE.f.flags = $(cc.flags) $(copt.flags) $(f.flags)
COMPILE.F.flags = $(cc.flags) $(copt.flags) $(cpp.flags) $(f.flags)
COMPILE.i.flags = $(cc.flags) $(copt.flags) $(c.flags)
COMPILE.ii.flags = $(cc.flags) $(copt.flags) $(cxx.flags)
LINK.flags = $(DEFAULT_LDFLAGS) $(CONFIG_LDFLAGS) $(LDFLAGS)
OBJECTIFY.c.flags = $(cc.flags) $(o.flags) $(S.flags) $(cpp.flags) $(copt.flags) $(c.flags)
OBJECTIFY.cxx.flags = $(cc.flags) $(o.flags) $(S.flags) $(cpp.flags) $(copt.flags) $(cxx.flags)
OBJECTIFY.s.flags = $(ASONLYFLAGS) $(s.flags)
OBJECTIFY.S.flags = $(cc.flags) $(o.flags) $(S.flags) $(cpp.flags)
OBJECTIFY.f.flags = $(cc.flags) $(o.flags) $(S.flags) $(f.flags)
OBJECTIFY.F.flags = $(cc.flags) $(o.flags) $(S.flags) $(cpp.flags) $(copt.flags) $(f.flags)
PREPROCESS.flags = -E $(copt.flags) $(cc.flags) $(cpp.flags)
PREPROCESS.lds.flags = -D__LINKER__ $(filter-out -g%,$(PREPROCESS.flags)) -P -xc

COMPILE.c = $(CC) -S $(COMPILE.c.flags)
COMPILE.cxx = $(CXX) -S $(COMPILE.cxx.flags)
COMPILE.i = $(CC) -S $(COMPILE.i.flags)
COMPILE.f = $(FC) -S $(COMPILE.f.flags)
COMPILE.F = $(FC) -S $(COMPILE.F.flags)
OBJECTIFY.s = $(AS) $(OBJECTIFY.s.flags)
OBJECTIFY.S = $(CC) $(OBJECTIFY.S.flags) -c
OBJECTIFY.f = $(FC) $(OBJECTIFY.f.flags) -c
OBJECTIFY.F = $(FC) $(OBJECTIFY.F.flags) -c
OBJECTIFY.c = $(CC) $(OBJECTIFY.c.flags) -c
OBJECTIFY.cxx = $(CXX) $(OBJECTIFY.cxx.flags) -c
PREPROCESS = $(CC) $(PREPROCESS.flags)
PREPROCESS.lds = $(CC) $(PREPROCESS.lds.flags)
LINK = $(LD) $(LINK.flags)
ELF = o/libc/elf/elf.lds
ELFLINK = $(COMPILE) -ALINK.elf $(LINK) $(LINKARGS) $(OUTPUT_OPTION) && $(COMPILE) -AFIXUP.ape -T$@ $(FIXUPOBJ) $@
LINKARGS = $(patsubst %.lds,-T %.lds,$(call uniqr,$(LD.libs) $(filter-out %.pkg,$^)))
LOLSAN = build/lolsan -b $(IMAGE_BASE_VIRTUAL)

# The compiler won't generate %xmm code for sources extensioned .greg.c,
# which is needed for C modules wanting to run at the executive level or
# during privileged runtime states, e.g. code morphing.
OBJECTIFY.greg.c =							\
	$(CC)								\
	$(filter-out -pg,$(OBJECTIFY.c.flags))				\
	-D__MGENERAL_REGS_ONLY__					\
	-mgeneral-regs-only						\
	-fno-stack-protector						\
	-fno-instrument-functions					\
	-fno-optimize-sibling-calls					\
	-fno-sanitize=all						\
	-ffreestanding							\
	-fwrapv								\
	-c

OBJECTIFY.ansi.c = $(CC) $(OBJECTIFY.c.flags) -ansi -Wextra -Werror -pedantic-errors -c
OBJECTIFY.c99.c = $(CC) $(OBJECTIFY.c.flags) -std=c99 -Wextra -Werror -pedantic-errors -c
OBJECTIFY.c11.c = $(CC) $(OBJECTIFY.c.flags) -std=c11 -Wextra -Werror -pedantic-errors -c
OBJECTIFY.c2x.c = $(CC) $(OBJECTIFY.c.flags) -std=c2x -Wextra -Werror -pedantic-errors -c

OBJECTIFY.real.c =							\
	$(GCC)								\
	-x-no-pg							\
	$(OBJECTIFY.c.flags)						\
	-wrapper build/realify.sh					\
	-D__REAL_MODE__							\
	-ffixed-r8							\
	-ffixed-r9							\
	-ffixed-r10							\
	-ffixed-r11							\
	-ffixed-r12							\
	-ffixed-r13							\
	-ffixed-r14							\
	-ffixed-r15							\
	-mno-red-zone							\
	-fcall-used-rbx							\
	-fno-jump-tables						\
	-fno-shrink-wrap						\
	-fno-schedule-insns2						\
	-flive-range-shrinkage						\
	-fno-omit-frame-pointer						\
	-momit-leaf-frame-pointer					\
	-mpreferred-stack-boundary=3					\
	-fno-delete-null-pointer-checks					\
	-c

OBJECTIFY.ncabi.c =							\
	$(GCC)								\
	$(OBJECTIFY.c.flags)						\
	-mno-sse							\
	-mfpmath=387							\
	-fno-stack-protector						\
	-fno-instrument-functions					\
	-fno-optimize-sibling-calls					\
	-fno-sanitize=all						\
	-fcall-saved-rcx						\
	-fcall-saved-rdx						\
	-fcall-saved-rdi						\
	-fcall-saved-rsi						\
	-fcall-saved-r8							\
	-fcall-saved-r9							\
	-fcall-saved-r10						\
	-fcall-saved-r11						\
	-c								\
	-xc

OBJECTIFY.initabi.c =							\
	$(GCC)								\
	$(OBJECTIFY.c.flags)						\
	-fno-stack-protector						\
	-fno-instrument-functions					\
	-fno-optimize-sibling-calls					\
	-fno-sanitize=all						\
	-fcall-saved-rdi						\
	-fcall-saved-rsi						\
	-c

TAGSFLAGS =								\
	-e								\
	-a								\
	--if0=no							\
	--langmap=c:.c.h.i						\
	--line-directives=yes						\
	--exclude=libc/nt/struct/imagefileheader.internal.h		\
	--exclude=libc/nt/struct/filesegmentelement.h
