
# note that order matters: make the libs first then use them 
SUBDIRS 		= complib libvendor opensm osmtest include $(DEFAULT_EVENT_PLUGIN)
DIST_SUBDIRS = complib libvendor opensm osmtest include osmeventplugin

# this will control the update of the files in order
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config-h.in 

ACLOCAL			= aclocal -I $(ac_aux_dir)

# we should provide a hint for other apps about the build mode of this project
install-exec-hook:
	mkdir -p $(DESTDIR)/$(includedir)
if DEBUG
	echo "define osm_build_type \"debug\"" > $(DESTDIR)/$(includedir)/infiniband/opensm/osm_build_id.h
else
	echo "define osm_build_type \"free\"" > $(DESTDIR)/$(includedir)/infiniband/opensm/osm_build_id.h
endif
	$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/init.d
	cp $(top_srcdir)/scripts/opensm.init $(DESTDIR)/$(sysconfdir)/init.d/opensm
	chmod 755 $(DESTDIR)/$(sysconfdir)/init.d/opensm


man_MANS = man/opensm.8 man/osmtest.8

EXTRA_DIST = opensm.spec scripts/opensm.init scripts/opensm.sysconfig $(man_MANS)

dist-hook: $(EXTRA_DIST)
	mkdir -p $(distdir)/scripts
	cp -r --parents $< $(distdir)
