## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

noinst_LIBRARIES = libsvz.a
sbin_PROGRAMS = svz

libsvz_a_SOURCES = \
	svz_zebra.c svzd.c svz_network.c svz_interface.c svz_packet.c svz_sisis.c svz_top.c svz_tunnel.c

noinst_HEADERS = \
	svz_zebra.h svzd.h svz_network.h svz_interface.h svz_packet.h svz_sisis.h svz_top.h svz_tunnel.h

svz_SOURCES = svz.c $(libsvz_a_SOURCES)

svz_LDADD = ../lib/libzebra.la @LIBCAP@

examplesdir = $(exampledir)
#dist_examples_DATA = sisisd.conf.sample

#EXTRA_DIST = sisis4-MIB.txt
