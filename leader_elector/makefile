CC = gcc
EXECUTABLES = leader_elector
SISIS_API_OBJECTS = ../tests/sisis_api.o ../tests/sisis_netlink.o
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

leader_elector: leader_elector.o $(SISIS_API_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ leader_elector.o $(SISIS_API_OBJECTS)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 
