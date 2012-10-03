SUBDIRS = \
	src

man_MANS = \
	corewatcher.8

corewatcherconfdir = $(sysconfdir)/corewatcher
dist_corewatcherconf_DATA = \
	gdb.command \
	corewatcher.conf

systemdunitdir = $(prefix)/lib/systemd/system
dist_systemdunit_DATA = src/corewatcher.service

EXTRA_DIST = \
	COPYING \
	README \
	$(man_MANS)
