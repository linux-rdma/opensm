
INCLUDES = -I$(srcdir)/../include \
	   -I$(includedir)/infiniband

lib_LTLIBRARIES = libibeventdb.la

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

libibeventdb_la_CFLAGS = -Wall $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1 -Wno-deprecated-declarations

if HAVE_LD_VERSION_SCRIPT
    libibeventdb_version_script = -Wl,--version-script=$(srcdir)/libibeventdb.map
else
    libibeventdb_version_script =
endif

libibeventdb_la_SOURCES =
libibeventdb_la_LDFLAGS = -version-info $(ibeventdb_api_version) \
	 -export-dynamic $(libibeventdb_version_script)
libibeventdb_la_LIBADD = -L../complib $(OSMV_LDADD) -losmcomp
libibeventdb_la_DEPENDENCIES = $(srcdir)/libibeventdb.map

libibeventdbincludedir = $(includedir)/infiniband/complib

libibeventdbinclude_HEADERS =

# headers are distributed as part of the include dir
EXTRA_DIST = $(srcdir)/libibeventdb.spec.in $(srcdir)/libibeventdb.map \
	$(srcdir)/libibeventdb.ver

dist-hook: libibeventdb.spec
	cp libibeventdb.spec $(distdir)

