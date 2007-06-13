
INCLUDES = -I$(srcdir)/../include

lib_LTLIBRARIES = libosmcomp.la

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

libosmcomp_la_CFLAGS = -Wall $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1 -Wno-deprecated-declarations

if HAVE_LD_VERSION_SCRIPT
    libosmcomp_version_script = -Wl,--version-script=$(srcdir)/libosmcomp.map
else
    libosmcomp_version_script =
endif

libosmcomp_la_SOURCES = cl_complib.c cl_dispatcher.c \
			cl_event.c cl_event_wheel.c \
			cl_list.c cl_log.c cl_map.c \
			cl_pool.c cl_ptr_vector.c \
			cl_spinlock.c cl_statustext.c \
			cl_thread.c cl_threadpool.c \
			cl_timer.c cl_vector.c \
			ib_statustext.c
libosmcomp_la_LDFLAGS = -version-info $(complib_api_version) \
	 -export-dynamic $(libosmcomp_version_script)
libosmcomp_la_DEPENDENCIES = $(srcdir)/libosmcomp.map

libosmcompincludedir = $(includedir)/infiniband/complib

libosmcompinclude_HEADERS = $(srcdir)/../include/complib/cl_atomic.h \
	$(srcdir)/../include/complib/cl_atomic_osd.h \
	$(srcdir)/../include/complib/cl_byteswap.h \
	$(srcdir)/../include/complib/cl_byteswap_osd.h \
	$(srcdir)/../include/complib/cl_comppool.h \
	$(srcdir)/../include/complib/cl_debug.h \
	$(srcdir)/../include/complib/cl_debug_osd.h \
	$(srcdir)/../include/complib/cl_dispatcher.h \
	$(srcdir)/../include/complib/cl_event.h \
	$(srcdir)/../include/complib/cl_event_wheel.h \
	$(srcdir)/../include/complib/cl_event_osd.h \
	$(srcdir)/../include/complib/cl_fleximap.h \
	$(srcdir)/../include/complib/cl_list.h \
	$(srcdir)/../include/complib/cl_log.h \
	$(srcdir)/../include/complib/cl_map.h \
	$(srcdir)/../include/complib/cl_math.h \
	$(srcdir)/../include/complib/cl_packoff.h \
	$(srcdir)/../include/complib/cl_packon.h \
	$(srcdir)/../include/complib/cl_passivelock.h \
	$(srcdir)/../include/complib/cl_pool.h \
	$(srcdir)/../include/complib/cl_ptr_vector.h \
	$(srcdir)/../include/complib/cl_qcomppool.h \
	$(srcdir)/../include/complib/cl_qlist.h \
	$(srcdir)/../include/complib/cl_qlockpool.h \
	$(srcdir)/../include/complib/cl_qmap.h \
	$(srcdir)/../include/complib/cl_qpool.h \
	$(srcdir)/../include/complib/cl_spinlock.h \
	$(srcdir)/../include/complib/cl_spinlock_osd.h \
	$(srcdir)/../include/complib/cl_thread.h \
	$(srcdir)/../include/complib/cl_thread_osd.h \
	$(srcdir)/../include/complib/cl_threadpool.h \
	$(srcdir)/../include/complib/cl_timer.h \
	$(srcdir)/../include/complib/cl_timer_osd.h \
	$(srcdir)/../include/complib/cl_types.h \
	$(srcdir)/../include/complib/cl_types_osd.h \
	$(srcdir)/../include/complib/cl_threadpool.h \
	$(srcdir)/../include/complib/cl_timer.h \
	$(srcdir)/../include/complib/cl_timer_osd.h \
	$(srcdir)/../include/complib/cl_types.h \
	$(srcdir)/../include/complib/cl_types_osd.h \
	$(srcdir)/../include/complib/cl_vector.h

# headers are distributed as part of the include dir
EXTRA_DIST = $(srcdir)/libosmcomp.spec.in $(srcdir)/libosmcomp.map \
	$(srcdir)/libosmcomp.ver

dist-hook: libosmcomp.spec
	cp libosmcomp.spec $(distdir)

# as we can not use libtool -release since it actually changes the SONAME 
# to the full release name instead of keeping it to the original
# name. What we can do it create a link to the installed file such
# that it will mark it with an alias... 
# we find the new lib by traversing the links
install-exec-hook:
	if test -L $(DESTDIR)/$(libdir)/libosmcomp-$(VERSION).so; then rm $(DESTDIR)/$(libdir)/libosmcomp-$(VERSION).so; fi; \
	lname=`\ls -l $(DESTDIR)/$(libdir)/libosmcomp.so | awk '{print $$NF}'`; \
	ln -s $$lname $(DESTDIR)/$(libdir)/libosmcomp-$(VERSION).so
