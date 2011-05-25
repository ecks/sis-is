CC = gcc
EXECUTABLES = test1 sys_stats
SISIS_API_OBJECTS = sisis_api.o sisis_netlink.o
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

test1: test1.o $(SISIS_API_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ test1.o $(SISIS_API_OBJECTS)

sys_stats: sys_stats.o $(SISIS_API_OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ sys_stats.o $(SISIS_API_OBJECTS)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

