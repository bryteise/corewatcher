all: corewatcher

CFLAGS = -O2 -g -Wall -W -D_FORTIFY_SOURCE=2 -fstack-protector

LIBS = corewatcher.o find_file.o
corewatcher: $(LIBS) coredumper.h Makefile
	gcc $(CFLAGS) $(LIBS) -o extract_core
	
clean:
	rm -f *.o extract_core DEADJOE corewatches *~
	
	