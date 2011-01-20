## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib @SNMP_INCLUDES@
DEFS = @DEFS@ -DSYSCONFDIR=\"$(sysconfdir)/\"
INSTALL_SDATA=@INSTALL@ -m 600

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

noinst_LIBRARIES = libsisis.a
sbin_PROGRAMS = sisisd

libsisis_a_SOURCES = \
	sisisd.c sisis_zebra.c sisis_main.c

noinst_HEADERS = \
	sisisd.h sisis_zebra.h

sisisd_SOURCES = sisis_main.c
sisisd_LDADD = libsisis.a ../lib/libzebra.la @LIBCAP@ @LIBM@

examplesdir = $(exampledir)
dist_examples_DATA = sisisd.conf.sample

EXTRA_DIST = sisis4-MIB.txt

