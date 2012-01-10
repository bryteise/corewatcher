SUBDIRS = \
	src

man_MANS = \
	corewatcher.8

EXTRA_DIST = \
	corewatcher.supp \
	$(man_MANS)

sysctldir = $(sysconfdir)/sysctl.d

corefileconfdir = $(sysconfdir)/security/limits.d
dist_corefileconf_DATA = \
	95-core.conf
