all: corewatcher corewatcher-applet

CFLAGS := -O2 -g -Wall -W -D_FORTIFY_SOURCE=2 -fstack-protector

CFLAGS += `pkg-config --cflags glib-2.0 gtk+-2.0 dbus-glib-1`  -fno-common

LINKFLAGS += `pkg-config --libs glib-2.0 dbus-glib-1` `curl-config --libs` -fno-common

LIBS = corewatcher.o find_file.o webinterface.o dbus.o configfile.o
corewatcher: $(LIBS) coredumper.h Makefile
	gcc $(CFLAGS) $(LIBS) $(LINKFLAGS) -o corewatcher
	
corewatcher-applet:
	gcc $(CFLAGS) $(LINKFLAGS)  `pkg-config --libs gtk+-2.0` corewatcher-applet.c -o corewatcher-applet -lnotify
	
clean:
	rm -f *.o extract_core DEADJOE corewatcher *~
	
	