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
	inotification.c \
	find_file.c \
	submit.c

noinst_HEADERS = \
	corewatcher.h

AM_CPPFLAGS = $(AM_CFLAGS) $(glib_CFLAGS) ${curl_CFLAGS}
corewatcher_LDADD = $(glib_LIBS) ${curl_LIBS} ${systemd_journal_LIBS}
