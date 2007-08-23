
SUBDIRS = .

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

INCLUDES = $(OSMV_INCLUDES)

lib_LTLIBRARIES = libosmvendor.la

if OSMV_OPENIB
libosmvendor_la_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT -DDUAL_SIDED_RMPP $(DBGFLAGS)
else
libosmvendor_la_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT $(DBGFLAGS)
endif

if HAVE_LD_VERSION_SCRIPT
    libosmvendor_version_script = -Wl,--version-script=$(srcdir)/libosmvendor.map
else
    libosmvendor_version_script =
endif

COMM_HDRS= $(srcdir)/../include/vendor/osm_vendor_api.h \
			$(srcdir)/../include/vendor/osm_vendor.h \
			$(srcdir)/../include/vendor/osm_vendor_select.h \
			$(srcdir)/../include/vendor/osm_vendor_sa_api.h \
			$(srcdir)/../include/vendor/osm_pkt_randomizer.h

if OSMV_OPENIB
libosmvendor_la_SOURCES = osm_vendor_ibumad.c \
			  osm_vendor_ibumad_sa.c \
			  osm_pkt_randomizer.c
HDRS =$(COMM_HDRS) $(srcdir)/../include/vendor/osm_vendor_ibumad.h
endif
if OSMV_SIM
libosmvendor_la_SOURCES = osm_vendor_mlx.c \
		osm_vendor_mlx_sim.c \
		osm_vendor_mlx_hca_sim.c \
		osm_vendor_mlx_dispatcher.c \
		osm_vendor_mlx_rmpp_ctx.c \
		osm_vendor_mlx_sar.c \
		osm_vendor_mlx_sender.c \
		osm_vendor_mlx_txn.c \
		osm_vendor_mlx_sa.c \
		osm_pkt_randomizer.c
HDRS =$(COMM_HDRS) $(srcdir)/../include/vendor/osm_vendor_mlx.h
endif
if OSMV_GEN1
libosmvendor_la_SOURCES = osm_vendor_mlx.c \
		osm_pkt_randomizer.c \
		osm_vendor_mlx_hca.c \
		osm_vendor_mlx_dispatcher.c \
		osm_vendor_mlx_rmpp_ctx.c \
		osm_vendor_mlx_sar.c \
		osm_vendor_mlx_sender.c \
		osm_vendor_mlx_ts.c \
		osm_vendor_mlx_txn.c \
		osm_vendor_mlx_sa.c
HDRS =$(COMM_HDRS) $(srcdir)/../include/vendor/osm_vendor_mlx.h
endif
if OSMV_VAPI
libosmvendor_la_SOURCES = osm_vendor_mlx.c \
		osm_pkt_randomizer.c \
		osm_vendor_mlx_hca.c \
		osm_vendor_mlx_dispatcher.c \
		osm_vendor_mlx_rmpp_ctx.c \
		osm_vendor_mlx_sar.c \
		osm_vendor_mlx_sender.c \
		osm_vendor_mlx_ibmgt.c \
		osm_vendor_mlx_txn.c \
		osm_vendor_mlx_sa.c
HDRS =$(COMM_HDRS) $(srcdir)/../include/vendor/osm_vendor_mlx.h
endif

libosmvendor_la_LIBADD = -L../complib -losmcomp
libosmvendor_la_LDFLAGS = -version-info $(osmvendor_api_version) \
	-export-dynamic $(libosmvendor_version_script)
libosmvendor_la_DEPENDENCIES = $(srcdir)/libosmvendor.map

libosmvendorincludedir = $(includedir)/infiniband/vendor

libosmvendorinclude_HEADERS = $(HDRS)

# headers are distributed as part of the include dir
EXTRA_DIST = libosmvendor.spec.in $(srcdir)/libosmvendor.map $(srcdir)/libosmvendor.ver

dist-hook: libosmvendor.spec
	cp libosmvendor.spec $(distdir)

# as we can not use libtool -release since it actually changes the SONAME
# to the full release name instead of keeping it to the original
# name. What we can do it create a link to the installed file such
# that it will mark it with an alias...
# we find the new lib by traversing the links
install-exec-hook:
	if test -L $(DESTDIR)/$(libdir)/libosmvendor-$(VERSION).so; then rm $(DESTDIR)/$(libdir)/libosmvendor-$(VERSION).so; fi; \
	lname=`\ls -l $(DESTDIR)/$(libdir)/libosmvendor.so | awk '{print $$NF}'`; \
	ln -s $$lname $(DESTDIR)/$(libdir)/libosmvendor_$(with_osmv).so; \
	ln -s $$lname $(DESTDIR)/$(libdir)/libosmvendor-$(VERSION).so
