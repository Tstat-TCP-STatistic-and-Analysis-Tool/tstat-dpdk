#rrd_merger makefile

CC=gcc
LIBS=-lxml2 -lrrd
CFLAGS=-Wall
INCLUDES= -I /usr/include/libxml2
LIBPATH= -L /usr/lib/x86_64-linux-gnu -L /usr/lib/i386-linux-gnu
INSTALL_PATH=/usr/bin

default: main.o
	$(CC) $(INCLUDES) -o rrd_merger main.o $(CFLAGS) $(LIBPATH) $(LIBS)

clean:
	rm -f main.o rrd_merger


    
install: default
	install -m 0755 rrd_merger $(INSTALL_PATH)

