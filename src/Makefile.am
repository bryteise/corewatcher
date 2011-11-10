sbin_PROGRAMS = \
	corewatcher \
	corewatcher-config

bin_SCRIPTS = \
	submit_core

corewatcher_SOURCES = \
	configfile.c \
	coredump.c \
	corewatcher.c \
	find_file.c \
	submit.c

corewatcher_config_SOURCES = \
	corewatcher-config.c

corewatcher_CFLAGS = \
	$(DBUS_CFLAGS) \
	$(DBUSGLIB_CFLAGS) \
	$(GLIB2_CFLAGS) \
	$(LIBPROXY_CFLAGS) \
	$(LIBNOTIFY_CFLAGS) \
	$(LIBCURL_CFLAGS)

corewatcher_LDADD = \
	$(DBUS_LIBS) \
	$(DBUSGLIB_LIBS) \
	$(GLIB2_LIBS) \
	$(LIBPROXY_LIBS) \
	$(LIBNOTIFY_LIBS) \
	$(LIBCURL_LIBS)

corewatcher_config_CFLAGS = \
	$(GLIB2_CFLAGS)
corewatcher_config_LDADD = \
	$(GLIB2_LIBS)

noinst_HEADERS = \
	corewatcher.h
