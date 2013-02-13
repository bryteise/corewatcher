SUBDIRS = \
	src

man_MANS = \
	corewatcher.8

corewatcherconfdir = $(sysconfdir)/corewatcher
dist_corewatcherconf_DATA = \
	gdb.command \
	corewatcher.conf

limitsdir = $(sysconfdir)/security/limits.d

dist_limits_DATA = 95-core.conf

systemdunitdir = $(prefix)/lib/systemd/system
dist_systemdunit_DATA = src/corewatcher.service

EXTRA_DIST = \
	COPYING \
	README \
	$(man_MANS)
