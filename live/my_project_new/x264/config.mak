prefix=/opt/usr/local/arm/x264
exec_prefix=${prefix}
bindir=${exec_prefix}/bin
libdir=${exec_prefix}/lib
includedir=${prefix}/include
ARCH=ARM
SYS=LINUX
CC=/opt/usr/local/arm/4.3.2/bin/arm-linux-gcc
CFLAGS=-Wshadow -O3 -fno-fast-math  -Wall -I. -std=gnu99 -fPIC -s -fomit-frame-pointer -fno-tree-vectorize
LDFLAGS= -lm -lpthread -Wl,-Bsymbolic -s
LDFLAGSCLI=
AR=/opt/usr/local/arm/4.3.2/bin/arm-linux-ar
RANLIB=/opt/usr/local/arm/4.3.2/bin/arm-linux-ranlib
STRIP=/opt/usr/local/arm/4.3.2/bin/arm-linux-strip
AS=
ASFLAGS= -DPIC -DBIT_DEPTH=8
EXE=
HAVE_GETOPT_LONG=1
DEVNULL=/dev/null
SOSUFFIX=so
SONAME=libx264.so.114
SOFLAGS=-Wl,-soname,$(SONAME)
default: $(SONAME)
