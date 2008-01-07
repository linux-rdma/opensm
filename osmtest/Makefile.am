
if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

INCLUDES = -I$(srcdir)/include $(OSMV_INCLUDES)

sbin_PROGRAMS = osmtest
osmtest_SOURCES = main.c osmtest.c osmt_service.c osmt_slvl_vl_arb.c \
		 osmt_multicast.c osmt_inform.c
if OSMV_VAPI
osmtest_SOURCES += osmt_mtl_regular_qp.c
endif
if OSMV_OPENIB
osmtest_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT -DDUAL_SIDED_RMPP $(DBGFLAGS)
else
osmtest_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT $(DBGFLAGS)
endif
osmtest_LDADD = -L../complib -L../libvendor -L../opensm -L$(libdir) -L. \
	$(OSMV_LDADD) -lopensm -losmcomp -losmvendor

osmtest_LDFLAGS = -Wl,--rpath -Wl,$(libdir) -lpthread  -L../opensm
# for linking with the simulator client library we have to use g++:
if OSMV_SIM
USEGPPLINK = $(LIBTOOL) --mode=link g++ $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@
osmtest$(EXEEXT): $(osmtest_OBJECTS) $(osmtest_DEPENDENCIES)
	@rm -f osmtest$(EXEEXT)
	$(USEGPPLINK) $(osmtest_LDFLAGS) $(osmtest_OBJECTS) $(osmtest_LDADD) $(LIBS)
endif

EXTRA_DIST = $(srcdir)/include/osmt_inform.h \
   $(srcdir)/include/osmtest_subnet.h \
   $(srcdir)/include/osmtest.h \
   $(srcdir)/include/osmt_mtl_regular_qp.h \
   $(srcdir)/include/osmtest_base.h
