# Dreamcast toolchain makefile    by Jim Ursetto
# adapted from Stalin's build script version 0.3
#
# Interesting parameters:
# erase=0|1                     Erase build directories on the fly to save space
# thread_model=single|posix     Set gcc threading model
# verbose=0|1                   Display
# makejobs=-jn                  Set the number of jobs for calls to make to n
#
# Interesting targets (you can 'make' any of these):
# all: build
# build: build-sh4 build-arm
# build-sh4: build-sh4-binutils build-sh4-gcc
# build-arm: build-arm-binutils build-arm-gcc
# build-sh4-gcc: build-sh4-gcc-pass1 build-sh4-newlib build-sh4-gcc-pass2
# build-arm-gcc: build-arm-gcc-pass1
# build-sh4-newlib: build-sh4-newlib-only
# gdb
# insight

# User configuration
sh_target=sh-elf
arm_target=arm-eabi
sh_prefix  := /opt/toolchains/naomi/$(sh_target)
arm_prefix := /opt/toolchains/naomi/$(arm_target)
sh_binutils_ver=2.34
sh_gcc_ver=9.3.0
newlib_ver=3.3.0
gdb_ver=9.1
insight_ver=6.8-1

# The ARM version of binutils/gcc is separated out as the
# particular CPU versions we need may not be available in
# newer versions of GCC.
arm_binutils_ver=2.34
arm_gcc_ver=8.4.0

# Threading model, either posix or single-threaded.
thread_model=single
erase=1
verbose=1

# Set this value to -jn where n is the number of jobs you want to run with make.
# If you only want one job, just set this to nothing (i.e, "makejobs=").
# Tracking down problems with multiple make jobs is much more difficult than
# with just one running at a time. So, if you run into trouble, then you should
# clear this variable and try again with just one job running. By default, this
# is set to number of CPU cores plus one.
makejobs=-j$(shell echo "processor" | cat /proc/cpuinfo - | grep processor | wc -l)

# Set the languages to build for pass 2 of building gcc for sh-elf. The default
# here is to build C and C++. For some reason at least on the current toolchain,
# Objective C, and Objective C++ fail to configure, so they are disabled by default.
pass2_languages=c,c++

# Change this if you don't have Bash installed in /bin
SHELL = /bin/bash

# GCC compiles fine with clang, but only if we use libstdc++ instead of libc++.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  ifneq ($(shell $(CXX) --version | grep clang),)
    CXX := "$(CXX) -stdlib=libstdc++"
  endif
endif

# Catch all...
ifeq ($(CXX),)
  CXX := g++
endif


# Makefile variables
install=$(prefix)/bin
pwd := $(shell pwd)
logdir := $(pwd)/logs
PATH := $(sh_prefix)/bin:$(arm_prefix)/bin:$(PATH)
binutils_dir=binutils-$(binutils_ver)
gcc_dir=gcc-$(sh_gcc_ver)
newlib_dir=newlib-$(newlib_ver)

all: build

# ---- }}}

# ---- build {{{

build: build-sh4 build-arm
build-sh4: build-sh4-binutils build-sh4-gcc
build-arm: build-arm-binutils build-arm-gcc
build-sh4-gcc: build-sh4-gcc-pass1 build-sh4-newlib build-sh4-gcc-pass2
build-arm-gcc: build-arm-gcc-pass1
	$(clean_arm_hack)
build-sh4-newlib: build-sh4-newlib-only

# Ensure that, no matter where we enter, prefix and target are set correctly.
build_sh4_targets=build-sh4-binutils build-sh4-gcc build-sh4-gcc-pass1 build-sh4-newlib build-sh4-newlib-only build-sh4-gcc-pass2
build_arm_targets=build-arm-binutils build-arm-gcc build-arm-gcc-pass1
$(build_sh4_targets): prefix = $(sh_prefix)
$(build_sh4_targets): target = $(sh_target)
$(build_sh4_targets): extra_configure_args = --with-multilib-list=m4-single-only --with-endian=little --with-cpu=m4-single-only
$(build_sh4_targets): gcc_ver = $(sh_gcc_ver)
$(build_sh4_targets): binutils_ver = $(sh_binutils_ver)
$(build_arm_targets): prefix = $(arm_prefix)
$(build_arm_targets): target = $(arm_target)
$(build_arm_targets): extra_configure_args = --with-arch=armv4 --with-mode=arm --disable-multilib
$(build_arm_targets): gcc_ver = $(arm_gcc_ver)
$(build_arm_targets): binutils_ver = $(arm_binutils_ver)

# To avoid code repetition, we use the same commands for both
# architectures.  But we can't create a single target called
# build-binutils for both sh4 and arm, because phony targets
# can't be run multiple times.  So we create multiple targets.
build_binutils     = build-sh4-binutils  build-arm-binutils
build_gcc_pass1    = build-sh4-gcc-pass1 build-arm-gcc-pass1
build_newlib       = build-sh4-newlib-only
build_gcc_pass2    = build-sh4-gcc-pass2

# Here we use the essentially same code for multiple targets,
# differing only by the current state of the variables below.
$(build_binutils): build = build-binutils-$(target)-$(binutils_ver)
$(build_binutils): src_dir = binutils-$(binutils_ver)
$(build_binutils): log = $(logdir)/$(build).log
$(build_binutils): logdir
	@echo "+++ Building $(src_dir) to $(build)..."
	-mkdir -p $(build)
	> $(log)
	cd $(build); ../$(src_dir)/configure --target=$(target) --prefix=$(prefix) --disable-werror CXX=$(CXX) $(to_log)
	$(MAKE) $(makejobs) -C $(build) DESTDIR=$(DESTDIR) $(to_log)
	$(MAKE) -C $(build) install DESTDIR=$(DESTDIR) $(to_log)
	$(clean_up)

$(build_gcc_pass1) $(build_gcc_pass2): build = build-gcc-$(target)-$(gcc_ver)
$(build_gcc_pass1) $(build_gcc_pass2): src_dir = gcc-$(gcc_ver)
$(build_gcc_pass1): log = $(logdir)/$(build)-pass1.log
$(build_gcc_pass1): logdir
	@echo "+++ Building $(src_dir) to $(build) (pass 1)..."
	-mkdir -p $(build)
	> $(log)
	cd $(build);  ../$(src_dir)/configure --target=$(target) --prefix=$(prefix) --without-headers --with-newlib --enable-languages=c --enable-libssp=no --enable-tls=no $(extra_configure_args) CXX=$(CXX) $(to_log)
	$(MAKE) $(makejobs) -C $(build) DESTDIR=$(DESTDIR) $(to_log)
	$(MAKE) -C $(build) install-strip DESTDIR=$(DESTDIR) $(to_log)

$(build_newlib): build = build-newlib-$(target)-$(newlib_ver)
$(build_newlib): src_dir = newlib-$(newlib_ver)
$(build_newlib): log = $(logdir)/$(build).log
$(build_newlib): logdir
	@echo "+++ Building $(src_dir) to $(build)..."
	patch -N -p0 < newlib.patch || true
	-mkdir -p $(build)
	> $(log)
	cd $(build); ../$(src_dir)/configure --target=$(target) --prefix=$(prefix) $(extra_configure_args) $(to_log)
	$(MAKE) $(makejobs) -C $(build) DESTDIR=$(DESTDIR) $(to_log)
	$(MAKE) -C $(build) install DESTDIR=$(DESTDIR) $(to_log)
	$(clean_up)

$(build_gcc_pass2): log = $(logdir)/$(build)-pass2.log
$(build_gcc_pass2): logdir
	@echo "+++ Building $(src_dir) to $(build) (pass 2)..."
	-mkdir -p $(build)
	> $(log)
	cd $(build);  ../$(src_dir)/configure --target=$(target) --prefix=$(prefix) --with-newlib --enable-libssp=no --enable-tls=no \
	   --enable-threads=$(thread_model) --enable-languages=$(pass2_languages) $(extra_configure_args) CXX=$(CXX) $(to_log)
	$(MAKE) $(makejobs) -C $(build) DESTDIR=$(DESTDIR) $(to_log)
	$(MAKE) -C $(build) install-strip DESTDIR=$(DESTDIR) $(to_log)
	$(clean_up)

# ---- }}}}


# GDB building

gdb-$(gdb_ver).tar.gz:
	@echo "+++ Downloading GDB..."
	wget -c ftp://ftp.gnu.org/gnu/gdb/gdb-$(gdb_ver).tar.gz

unpack_gdb: gdb-$(gdb_ver).tar.gz unpack_gdb_stamp

unpack_gdb_stamp:
	@echo "+++ Unpacking GDB..."
	rm -f $@
	rm -rf gdb-$(gdb_ver)
	tar xf gdb-$(gdb_ver).tar.gz
	touch $@

build_gdb: log = $(logdir)/gdb-$(gdb_ver).log
build_gdb: logdir
build_gdb: unpack_gdb build_gdb_stamp

build_gdb_stamp:
	@echo "+++ Building GDB..."
	rm -f $@
	> $(log)
	rm -rf build-gdb-$(gdb_ver)
	mkdir build-gdb-$(gdb_ver)
	cd build-gdb-$(gdb_ver); ../gdb-$(gdb_ver)/configure \
	    --disable-werror \
	    --prefix=$(sh_prefix) \
	    --target=$(sh_target) $(to_log)
	$(MAKE) $(makejobs) -C build-gdb-$(gdb_ver) $(to_log)
	touch $@

install_gdb: log = $(logdir)/gdb-$(gdb_ver).log
install_gdb: logdir
install_gdb: build_gdb install_gdb_stamp

install_gdb_stamp:
	@echo "+++ Installing GDB..."
	rm -f $@
	$(MAKE) -C build-gdb-$(gdb_ver) install DESTDIR=$(DESTDIR) $(to_log)
	touch $@

gdb: install_gdb


# Insight building

insight-$(insight_ver)a.tar.bz2:
	@echo "+++ Downloading Insight..."
	wget -c ftp://sourceware.org/pub/insight/releases/insight-$(insight_ver)a.tar.bz2

unpack_insight: insight-$(insight_ver)a.tar.bz2 unpack_insight_stamp

unpack_insight_stamp:
	@echo "+++ Unpacking Insight..."
	rm -f $@
	rm -rf insight-$(insight_ver)
	tar xf insight-$(insight_ver)a.tar.bz2
	touch $@

build_insight: log = $(logdir)/insight-$(insight_ver).log
build_insight: logdir
build_insight: unpack_insight build_insight_stamp

build_insight_stamp:
	@echo "+++ Building Insight..."
	rm -f $@
	> $(log)
	rm -rf build-insight-$(insight_ver)
	mkdir build-insight-$(insight_ver)
	cd build-insight-$(insight_ver); ../insight-$(insight_ver)/configure \
	    --disable-werror \
	    --prefix=$(sh_prefix) \
	    --target=$(sh_target) $(to_log)
	$(MAKE) $(makejobs) -C build-insight-$(insight_ver) $(to_log)
	touch $@

install_insight: log = $(logdir)/insight-$(insight_ver).log
install_insight: logdir
install_insight: build_insight install_insight_stamp

install_insight_stamp:
	@echo "+++ Installing Insight..."
	rm -f $@
	$(MAKE) -C build-insight-$(insight_ver) install DESTDIR=$(DESTDIR) $(to_log)
	touch $@

insight: install_insight


# ---- support {{{

clean:
	-rm -rf build-newlib-$(sh_target)-$(newlib_ver)
	-rm -rf build-newlib-$(arm_target)-$(newlib_ver)
	-rm -rf build-gcc-$(sh_target)-$(gcc_ver)
	-rm -rf build-gcc-$(arm_target)-$(gcc_ver)
	-rm -rf build-binutils-$(sh_target)-$(binutils_ver)
	-rm -rf build-binutils-$(arm_target)-$(binutils_ver)
	-rm -rf build-gdb-$(gdb_ver) gdb-$(gdb_ver) gdb-$(gdb_ver).tar.gz install_gdb_stamp build_gdb_stamp unpack_gdb_stamp
	-rm -rf build-insight-$(insight_ver) install_insight_stamp build_insight_stamp unpack_insight_stamp

logdir:
	@mkdir -p $(logdir)

# If erase=1, erase build directories on the fly.
ifeq (1,$(erase))
  define clean_up
    @echo "+++ Cleaning up $(build)..."
    -rm -rf $(build)
  endef
  # Hack to clean up ARM gcc pass 1
  define clean_arm_hack
    @echo "+++ Cleaning up build-gcc-$(arm_target)-$(gcc_ver)..."
    -rm -rf build-gcc-$(arm_target)-$(gcc_ver)
  endef
endif

# If verbose=1, display output to screen as well as log files
ifeq (1,$(verbose))
  to_log = 2>&1 | tee -a $(log) && [ $$PIPESTATUS -eq 0 ]
else
  to_log = >> $(log) 2>&1
endif

# ---- }}}

# ---- phony targets {{{

.PHONY: all build build-sh4 build-arm $(build_sh4_targets) $(build_arm_targets) clean
.PHONY: build-binutils build-newlib build-gcc-pass1 build-gcc-pass2
.PHONY: gdb install_gdb build_gdb unpack_gdb
.PHONY: insight install_insight build_insight unpack_insight

# ---- }}}}

# vim:tw=0:fdm=marker:fdc=2:fdl=1
