
INCLUDES = $(OSMV_INCLUDES)

lib_LTLIBRARIES = libopensm.la

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS = -g
endif

if OSMV_OPENIB
libopensm_la_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT -DDUAL_SIDED_RMPP $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
else
libopensm_la_CFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
endif

if HAVE_LD_VERSION_SCRIPT
libopensm_version_script = -Wl,--version-script=$(srcdir)/libopensm.map
else
libopensm_version_script =
endif

libopensm_la_SOURCES = osm_log.c osm_mad_pool.c osm_helper.c
libopensm_la_LDFLAGS = -version-info $(opensm_api_version) \
	-export-dynamic $(libopensm_version_script)
libopensm_la_DEPENDENCIES = $(srcdir)/libopensm.map

sbin_PROGRAMS = opensm
opensm_SOURCES = main.c osm_console.c osm_db_files.c \
		 osm_db_pack.c osm_drop_mgr.c osm_fwd_tbl.c \
		 osm_inform.c osm_lid_mgr.c osm_lin_fwd_rcv.c \
		 osm_lin_fwd_tbl.c osm_link_mgr.c osm_mcast_fwd_rcv.c \
		 osm_mcast_mgr.c osm_mcast_tbl.c osm_mcm_info.c \
		 osm_mcm_port.c osm_mtree.c osm_multicast.c osm_node.c \
		 osm_node_desc_rcv.c osm_node_info_rcv.c \
		 osm_opensm.c osm_pkey.c osm_pkey_mgr.c osm_pkey_rcv.c \
		 osm_port.c osm_port_info_rcv.c \
		 osm_remote_sm.c osm_req.c \
		 osm_resp.c osm_sa.c osm_sa_class_port_info.c \
		 osm_sa_informinfo.c osm_sa_lft_record.c osm_sa_mft_record.c \
		 osm_sa_link_record.c osm_sa_mad_ctrl.c \
		 osm_sa_mcmember_record.c osm_sa_node_record.c \
		 osm_sa_path_record.c osm_sa_pkey_record.c \
		 osm_sa_portinfo_record.c osm_sa_guidinfo_record.c \
		 osm_sa_multipath_record.c osm_sa_response.c \
		 osm_sa_service_record.c osm_sa_slvl_record.c \
		 osm_sa_sminfo_record.c osm_sa_vlarb_record.c \
		 osm_sa_sw_info_record.c osm_service.c \
		 osm_slvl_map_rcv.c osm_sm.c osm_sminfo_rcv.c \
		 osm_sm_mad_ctrl.c osm_sm_state_mgr.c osm_state_mgr.c \
		 osm_state_mgr_ctrl.c osm_subnet.c \
		 osm_sweep_fail_ctrl.c osm_sw_info_rcv.c osm_switch.c \
		 osm_prtn.c osm_prtn_config.c osm_qos.c osm_router.c \
		 osm_trap_rcv.c osm_ucast_mgr.c osm_ucast_updn.c \
		 osm_ucast_lash.c osm_ucast_file.c osm_ucast_ftree.c \
		 osm_vl15intf.c osm_vl_arb_rcv.c \
		 st.c osm_perfmgr.c osm_event_db.c
if OSMV_OPENIB
opensm_CFLAGS = -Wall $(OSMV_CFLAGS) -fno-strict-aliasing -DVENDOR_RMPP_SUPPORT -DDUAL_SIDED_RMPP $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
opensm_CXXFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT -DDUAL_SIDED_RMPP $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
else
opensm_CFLAGS = -Wall $(OSMV_CFLAGS) -fno-strict-aliasing -DVENDOR_RMPP_SUPPORT $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
opensm_CXXFLAGS = -Wall $(OSMV_CFLAGS) -DVENDOR_RMPP_SUPPORT $(DBGFLAGS) -D_XOPEN_SOURCE=600 -D_BSD_SOURCE=1
endif

# for linking with the simulator client library we have to use g++:
if OSMV_SIM
USEGPPLINK = $(LIBTOOL) --mode=link g++ $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@ 
libopensm.la: $(libopensm_la_OBJECTS) $(libopensm_la_DEPENDENCIES) 
	$(USEGPPLINK) -rpath $(libdir) $(libopensm_la_LDFLAGS) $(libopensm_la_OBJECTS) $(libopensm_la_LIBADD) $(LIBS)
opensm$(EXEEXT): $(opensm_OBJECTS) $(opensm_DEPENDENCIES) 
	@rm -f opensm$(EXEEXT)
	$(USEGPPLINK) $(opensm_LDFLAGS) $(opensm_OBJECTS) $(opensm_LDADD) $(LIBS)
endif

# we need to be able to load libraries from local build subtree before make install
# we always give precedence to local tree libs and then use the pre-installed ones.
opensm_LDADD = -L../complib -L../libvendor -L. $(OSMV_LDADD) -lopensm -losmcomp -losmvendor

opensm_LDFLAGS = -Wl,--rpath -Wl,$(libdir) -lpthread -ldl

opensmincludedir = $(includedir)/infiniband/opensm

opensminclude_HEADERS = $(srcdir)/../include/opensm/osm_base.h \
			$(srcdir)/../include/opensm/osm_log.h \
			$(srcdir)/../include/opensm/osm_path.h \
			$(srcdir)/../include/opensm/osm_madw.h \
			$(srcdir)/../include/opensm/osm_mad_pool.h \
			$(srcdir)/../include/opensm/osm_msgdef.h \
			$(srcdir)/../include/opensm/osm_helper.h

# headers are distributed as part of the include dir
EXTRA_DIST = $(srcdir)/libopensm.map $(srcdir)/libopensm.ver \
	     $(srcdir)/ChangeLog

# as we can not use libtool -release since it actually changes the SONAME 
# to the full release name instead of keeping it to the original
# name. What we can do it create a link to the installed file such
# that it will mark it with an alias... 
# we find the new lib by traversing the links
install-exec-hook:
	if test -L $(DESTDIR)/$(libdir)/libopensm-$(VERSION).so; then rm $(DESTDIR)/$(libdir)/libopensm-$(VERSION).so; fi; \
	lname=`\ls -l $(DESTDIR)/$(libdir)/libopensm.so | awk '{print $$NF}'`; \
	ln -s $$lname $(DESTDIR)/$(libdir)/libopensm-$(VERSION).so
