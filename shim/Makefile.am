## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

noinst_LIBRARIES = libshim.a
sbin_PROGRAMS = shim

libshim_a_SOURCES = \
	shim_zebra.c shimd.c shim_network.c shim_interface.c shim_packet.c shim_sisis.c shim_top.c

noinst_HEADERS = \
	shim_zebra.h shimd.h shim_network.h shim_interface.h shim_packet.h shim_sisis.h shim_top.h

shim_SOURCES = shim.c $(libshim_a_SOURCES)

shim_LDADD = ../lib/libzebra.la @LIBCAP@

examplesdir = $(exampledir)
#dist_examples_DATA = sisisd.conf.sample

#EXTRA_DIST = sisis4-MIB.txt
