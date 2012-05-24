## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

noinst_LIBRARIES = libsv.a
sbin_PROGRAMS = sv

libsv_a_SOURCES = \
	sv_zebra.c svd.c sv_network.c sv_interface.c sv_packet.c sv_sisis.c sv_top.c

noinst_HEADERS = \
	sv_zebra.h svd.h sv_network.h sv_interface.h sv_packet.h sv_sisis.h sv_top.h

sv_SOURCES = sv.c $(libsv_a_SOURCES)

sv_LDADD = ../lib/libzebra.la @LIBCAP@

examplesdir = $(exampledir)
#dist_examples_DATA = sisisd.conf.sample

#EXTRA_DIST = sisis4-MIB.txt
