all: extract_core

CFLAGS = -O2 -g -Wall -W -D_FORTIFY_SOURCE=2 -fstack-protector

extract_core: extract_core.o find_file.o coredumper.h Makefile
	gcc $(CFLAGS) extract_core.o find_file.o -o extract_core
	
clean:
	rm -f *.o extract_core DEADJOE
	