#rrd_merger makefile

CC=gcc
LIBS=-lxml2 -lrrd
CFLAGS=-Wall

default: main.o
	$(CC) -o rrd_merger main.o $(CFLAGS) $(LIBS)

clean:
	rm -f main.o rrd_merger
