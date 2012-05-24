CC = gcc
EXECUTABLES = machine_monitor
SISIS_API_OBJECTS = ../tests/sisis_api.o ../tests/sisis_netlink.o
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

machine_monitor: machine_monitor.o $(SISIS_API_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ machine_monitor.o $(SISIS_API_OBJECTS)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

