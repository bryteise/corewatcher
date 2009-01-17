all: corewatcher

CFLAGS := -O2 -g -Wall -W -D_FORTIFY_SOURCE=2 -fstack-protector

CFLAGS += `pkg-config --cflags glib-2.0` `pkg-config --cflags dbus-glib-1` -fno-common
LINKFLAGS += `pkg-config --libs glib-2.0` `pkg-config --libs dbus-glib-1`  `curl-config --libs` -fno-common

LIBS = corewatcher.o find_file.o webinterface.o dbus.o configfile.o
corewatcher: $(LIBS) coredumper.h Makefile
	gcc $(CFLAGS) $(LIBS) $(LINKFLAGS) -o corewatcher
	
clean:
	rm -f *.o extract_core DEADJOE corewatcher *~
	
	