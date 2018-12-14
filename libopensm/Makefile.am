
AM_CPPFLAGS = -I$(srcdir)/../include

lib_LTLIBRARIES = libopensm.la

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

libopensm_la_CFLAGS = -Wall -Wwrite-strings $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_GNU_SOURCE=1

if HAVE_LD_VERSION_SCRIPT
    libopensm_version_script = -Wl,--version-script=$(srcdir)/libopensm.map
else
    libopensm_version_script =
endif

opensm_api_version=$(shell grep LIBVERSION= $(srcdir)/libopensm.ver | sed 's/LIBVERSION=//')

libopensm_la_SOURCES = osm_log.c osm_helper.c

libopensm_la_LIBADD = -L../complib -losmcomp
libopensm_la_LDFLAGS = -version-info $(opensm_api_version) \
	 -export-dynamic $(libopensm_version_script)
libopensm_la_DEPENDENCIES = $(srcdir)/libopensm.map

EXTRA_DIST = $(srcdir)/libopensm.map $(srcdir)/libopensm.ver
