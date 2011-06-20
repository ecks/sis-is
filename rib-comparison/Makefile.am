## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib
DEFS = @DEFS@ -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

#noinst_LIBRARIES = librc.a
sbin_PROGRAMS = shim

#librc_a_SOURCES = \
#	r_lsdb.c r_lsa.c

#noinst_HEADERS = \
#	shim.h

shim_SOURCES = shim.c
#rc_LDADD = librc.a ../lib/libzebra.la @LIBCAP@ @LIBM@

examplesdir = $(exampledir)
#dist_examples_DATA = sisisd.conf.sample

#EXTRA_DIST = sisis4-MIB.txt

