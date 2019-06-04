
AM_CPPFLAGS = -I$(srcdir)/../include \
	      -I$(includedir)/infiniband

lib_LTLIBRARIES = libosmroutingplugin.la

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

libosmroutingplugin_la_CFLAGS = -Wall -Wwrite-strings $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_GNU_SOURCE=1

if HAVE_LD_VERSION_SCRIPT
    libosmroutingplugin_version_script = -Wl,--version-script=$(srcdir)/libosmroutingplugin.map
else
    libosmroutingplugin_version_script =
endif

osmroutingplugin_api_version=$(shell grep LIBVERSION= $(srcdir)/libosmroutingplugin.ver | sed 's/LIBVERSION=//')

libosmroutingplugin_la_SOURCES = src/osmroutingplugin.c
libosmroutingplugin_la_LDFLAGS = -version-info $(osmroutingplugin_api_version) \
	 -export-dynamic $(libosmroutingplugin_version_script)
libosmroutingplugin_la_LIBADD = -L../complib -losmcomp -L../libopensm -lopensm $(OSMV_LDADD)
libosmroutingplugin_la_DEPENDENCIES = $(srcdir)/libosmroutingplugin.map

libosmroutingpluginincludedir = $(includedir)/infiniband/complib

libosmroutingplugininclude_HEADERS =

# headers are distributed as part of the include dir
EXTRA_DIST = $(srcdir)/libosmroutingplugin.map $(srcdir)/libosmroutingplugin.ver
