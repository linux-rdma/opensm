
# note that order matters: make the libs first then use them
SUBDIRS = complib libvendor opensm osmtest include $(DEFAULT_EVENT_PLUGIN)
DIST_SUBDIRS = complib libvendor opensm osmtest include osmeventplugin

ACLOCAL_AMFLAGS = -I config

# we should provide a hint for other apps about the build mode of this project
install-exec-hook:
	$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/init.d
	cp $(top_builddir)/scripts/opensm.init $(DESTDIR)/$(sysconfdir)/init.d/opensmd
	chmod 755 $(DESTDIR)/$(sysconfdir)/init.d/opensmd


man_MANS = man/opensm.8 man/osmtest.8 man/torus-2QoS.8 man/torus-2QoS.conf.5

various_scripts = $(wildcard scripts/*)
docs = doc/performance-manager-HOWTO.txt doc/QoS_management_in_OpenSM.txt \
	doc/opensm_release_notes-3.3.txt

EXTRA_DIST = autogen.sh opensm.spec $(various_scripts) $(man_MANS) $(docs)

dist-hook: $(EXTRA_DIST)
	if [ -x $(top_srcdir)/gen_chlog.sh ] ; then \
		cd $(top_srcdir); ./gen_chlog.sh > $(distdir)/ChangeLog ; cd - ; \
	fi
