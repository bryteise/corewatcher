SUBDIRS = \
	src

man_MANS = \
	corewatcher.8

corewatcherconfdir = $(sysconfdir)/corewatcher
dist_corewatcherconf_DATA = \
	gdb.command \
	corewatcher.conf

EXTRA_DIST = \
	corewatcher.supp \
	$(man_MANS)

sysctldir = $(sysconfdir)/sysctl.d
dist_sysctl_DATA = \
	corewatcher-core.conf

dbusdir = $(sysconfdir)/dbus-1/system.d
dist_dbus_DATA = \
	corewatcher.dbus

corefileconfdir = $(sysconfdir)/security/limits.d
dist_corefileconf_DATA = \
	95-core.conf