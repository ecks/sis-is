## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

#noinst_LIBRARIES = librc.a
sbin_PROGRAMS = shim

#librc_a_SOURCES = \
#	r_lsdb.c r_lsa.c

#noinst_HEADERS = \
#	shim.h

shim_SOURCES = shim.c shimd.c shim_network.c shim_packet.c
#rc_LDADD = librc.a ../lib/libzebra.la @LIBCAP@ @LIBM@
shim_LDADD = ../lib/libzebra.la @LIBCAP@

examplesdir = $(exampledir)
#dist_examples_DATA = sisisd.conf.sample

#EXTRA_DIST = sisis4-MIB.txt

