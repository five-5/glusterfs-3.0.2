xlator_LTLIBRARIES = qos_monitor.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

qos_monitor_la_LDFLAGS = -module -avoidversion

qos_monitor_la_SOURCES = qos_monitor.c
qos_monitor_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = qos_monitor.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-lhiredis -lpthread 

CLEANFILES = 
