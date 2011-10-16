CC = gcc
EXECUTABLES = remote_spawn
SISIS_API_OBJECTS = ../tests/sisis_api.o ../tests/sisis_netlink.o
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

remote_spawn: remote_spawn.o $(SISIS_API_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ remote_spawn.o $(SISIS_API_OBJECTS)

.c.o: 
	gcc -ggdb -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 
