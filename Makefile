VERSION = 0.27
#
# to build this package, you need to have the following components installed:
# dbus-glib-devel libnotify-devel curl-devel
#
PREFIX    ?= /usr
BINDIR     = $(PREFIX)/bin
SBINDIR    = $(PREFIX)/sbin
MANDIR     = $(PREFIX)/share/man/man8
CC ?= gcc

CFLAGS := -g -fstack-protector -D_FORTIFY_SOURCE=2 -Wall -pedantic -W -Wstrict-prototypes -Wundef -fno-common -Werror-implicit-function-declaration -Wdeclaration-after-statement -Wformat -Wformat-security -Werror=format-security

MY_CFLAGS := `pkg-config --cflags libnotify libproxy-1.0 glib-2.0 dbus-1`
#
# pkg-config tends to make programs pull in a ton of libraries, not all
# are needed. -Wl,--as-needed tells the linker to just drop unused ones.
#
LDF_C := -Wl,--as-needed `pkg-config --libs glib-2.0`
LDF_D := -Wl,--as-needed `pkg-config --libs glib-2.0 dbus-glib-1 libproxy-1.0 dbus-1` `curl-config --libs` -Wl,"-z relro" -Wl,"-z now"

all:	corewatcher corewatcher-config corewatcher.8.gz

.c.o:
	$(CC) $(CFLAGS) $(MY_CFLAGS) -c -o $@ $<


corewatcher:	corewatcher.o submit.o coredump.o configfile.o find_file.o corewatcher.h
	gcc corewatcher.o submit.o coredump.o configfile.o find_file.o $(LDF_D) -o corewatcher

corewatcher-config: corewatcher-config.o
	gcc corewatcher-config.o $(LDF_C)-o corewatcher-config

corewatcher.8.gz: corewatcher.8
	gzip -9 -c $< > $@

clean:
	rm -f *~ *.o *.ko DEADJOE corewatcher corewatcher-config *.out */*~ corewatcher.8.gz

install-system: corewatcher.8.gz
	-mkdir -p $(DESTDIR)$(MANDIR)
	-mkdir -p $(DESTDIR)/var/lib/corewatcher
	-mkdir -p $(DESTDIR)/etc/security/limits.d/
	-mkdir -p $(DESTDIR)/etc/dbus-1/system.d/
	-mkdir -p $(DESTDIR)/etc/sysctl.d/
	install -m 0644 95-core.conf $(DESTDIR)/etc/security/limits.d/95-core.conf
	if [ ! -f $(DESTDIR)/etc/corewatcher.conf ] ; then install -m 0644 corewatcher.conf $(DESTDIR)/etc/corewatcher.conf ; fi
	install -m 0644 corewatcher.dbus $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0644 corewatcher.8.gz $(DESTDIR)$(MANDIR)/
	install -m 0644 gdb.command  $(DESTDIR)/var/lib/corewatcher/
	install -m 0644 corewatcher.sysctl  $(DESTDIR)/etc/sysctl.d/corewatcher.conf

install-corewatcher: corewatcher
	-mkdir -p $(DESTDIR)$(SBINDIR)
	install -m 0755 corewatcher $(DESTDIR)$(SBINDIR)/
	install -m 0755 corewatcher-config $(DESTDIR)$(SBINDIR)/

install: install-system install-corewatcher

tests: corewatcher
	desktop-file-validate *.desktop
	for i in test/*txt ; do echo -n . ; ./corewatcher --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo
	[ -e /usr/bin/valgrind ] && for i in test/*txt ; do echo -n . ; valgrind -q --leak-check=full ./corewatcher --debug $$i > $$i.dbg ; diff -u $$i.out $$i.dbg ; done ; echo

valgrind: corewatcher tests
	valgrind -q --leak-check=full ./corewatcher --debug test/*.txt

dist:
	git tag v$(VERSION)
	git archive --format=tar --prefix="corewatcher-$(VERSION)/" v$(VERSION) | \
		gzip > corewatcher-$(VERSION).tar.gz
