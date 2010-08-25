VERSION = 0.9
#
# to build this package, you need to have the following components installed:
# dbus-glib-devel libnotify-devel gtk2-devel curl-devel
#
PREFIX    ?= /usr
BINDIR     = $(PREFIX)/bin
SBINDIR    = $(PREFIX)/sbin
LOCALESDIR = $(PREFIX)/share/locale
MANDIR     = $(PREFIX)/share/man/man8
CC ?= gcc

CFLAGS := -O2 -g -fstack-protector -D_FORTIFY_SOURCE=2 -Wall -W -Wstrict-prototypes -Wundef -fno-common -Werror-implicit-function-declaration -Wdeclaration-after-statement -Wformat -Wformat-security -Werror=format-security

MY_CFLAGS := `pkg-config --cflags libnotify gtk+-2.0`
#
# pkg-config tends to make programs pull in a ton of libraries, not all
# are needed. -Wl,--as-needed tells the linker to just drop unused ones,
# and that makes the applet load faster and use less memory.
#
LDF_A := -Wl,--as-needed `pkg-config --libs libnotify gtk+-2.0`
LDF_D := -Wl,--as-needed `pkg-config --libs glib-2.0 dbus-glib-1` `curl-config --libs` -Wl,"-z relro" -Wl,"-z now"

all:	corewatcher corewatcher-applet corewatcher.8.gz

noui:	corewatcher corewatcher.8.gz

.c.o:
	$(CC) $(CFLAGS) $(MY_CFLAGS) -c -o $@ $<


corewatcher:	corewatcher.o submit.o coredump.o configfile.o find_file.o corewatcher.h
	gcc corewatcher.o submit.o coredump.o configfile.o find_file.o $(LDF_D) -o corewatcher
	@(cd po/ && $(MAKE))

corewatcher-applet: corewatcher-applet.o
	gcc corewatcher-applet.o $(LDF_A)-o corewatcher-applet

corewatcher.8.gz: corewatcher.8
	gzip -9 -c $< > $@

clean:
	rm -f *~ *.o *.ko DEADJOE corewatcher corewatcher-applet *.out */*~ corewatcher.8.gz
	@(cd po/ && $(MAKE) $@)


install-system: corewatcher.8.gz
	-mkdir -p $(DESTDIR)$(MANDIR)
	-mkdir -p $(DESTDIR)/var/lib/corewatcher
	-mkdir -p $(DESTDIR)/etc/security/limits.d/
	-mkdir -p $(DESTDIR)/etc/init.d
	-mkdir -p $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0644 95-core.conf $(DESTDIR)/etc/security/limits.d/95-core.conf
	if [ ! -f $(DESTDIR)/etc/corewatcher.conf ] ; then install -m 0644 corewatcher.conf $(DESTDIR)/etc/corewatcher.conf ; fi
	install -m 0644 corewatcher.dbus $(DESTDIR)/etc/dbus-1/system.d/
	install -m 0644 corewatcher.8.gz $(DESTDIR)$(MANDIR)/
	install -m 0744 corewatcher.init $(DESTDIR)/etc/init.d/corewatcher
	install -m 0644 gdb.command  $(DESTDIR)/var/lib/corewatcher/
	@(cd po/ && env LOCALESDIR=$(LOCALESDIR) DESTDIR=$(DESTDIR) $(MAKE) install)

install-corewatcher: corewatcher
	-mkdir -p $(DESTDIR)$(SBINDIR)
	install -m 0755 corewatcher $(DESTDIR)$(SBINDIR)/

install-applet: corewatcher-applet
	-mkdir -p $(DESTDIR)$(BINDIR)
	-mkdir -p $(DESTDIR)/etc/xdg/autostart
	-mkdir -p $(DESTDIR)/usr/share/corewatcher
	install -m 0755 corewatcher-applet $(DESTDIR)$(BINDIR)/
	desktop-file-install --mode 0644 --dir=$(DESTDIR)/etc/xdg/autostart/ corewatcher-applet.desktop
	install -m 0644 icon.png $(DESTDIR)/usr/share/corewatcher/icon.png

install: install-system install-corewatcher install-applet

install-noui: install-system install-corewatcher


# This is for translators. To update your po with new strings, do :
# svn up ; make uptrans LG=fr # or de, ru, hu, it, ...
uptrans:
	xgettext -C -s -k_ -o po/corewatcher.pot *.c *.h
	@(cd po/ && env LG=$(LG) $(MAKE) $@)


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
