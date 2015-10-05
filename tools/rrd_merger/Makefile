#rrd_merger makefile

CC=gcc
LIBS=-lxml2 -lrrd
CFLAGS=-Wall
INSTALL_PATH=/usr/bin

default: main.o
	$(CC) -o rrd_merger main.o $(CFLAGS) $(LIBS)

clean:
	rm -f main.o rrd_merger


    
install: default
	install -m 0755 rrd_merger $(INSTALL_PATH)

