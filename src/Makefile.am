AM_CFLAGS = -std=gnu99 -fstack-protector -D_FORTIFY_SOURCE=2 \
	-Wall -pedantic -W -Wstrict-prototypes -Wundef -fno-common \
	-Werror-implicit-function-declaration \
	-Wdeclaration-after-statement -Wformat \
	-Wformat-security -Werror=format-security

sbin_PROGRAMS = \
	corewatcher

corewatcher_SOURCES = \
	configfile.c \
	coredump.c \
	corewatcher.c \
	find_file.c \
	submit.c

corewatcher_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBPROXY_CFLAGS) \
	$(LIBNOTIFY_CFLAGS) \
	$(LIBCURL_CFLAGS)

corewatcher_LDADD = \
	$(GLIB2_LIBS) \
	$(LIBPROXY_LIBS) \
	$(LIBNOTIFY_LIBS) \
	$(LIBCURL_LIBS)

noinst_HEADERS = \
	corewatcher.h
