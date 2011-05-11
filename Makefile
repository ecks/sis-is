CC = gcc
EXECUTABLES = shim sort join voter stop_redundancy
USER_OBJS   = shim.o voter.o sort.o join.o table.o
SISIS_API_C = ../tests/sisis_*.c
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

#sort: $(USER_OBJS)
#	$(CC) $(CFLAGS) -o sort $(USER_OBJS)

shim: shim.o table.o
	$(CC) $(CFLAGS) $(LIBS) -o shim shim.o table.o $(SISIS_API_C)

sort: sort.o table.o
	$(CC) $(CFLAGS) $(LIBS) -o sort sort.o table.o $(SISIS_API_C)

join: join.o table.o
	$(CC) $(CFLAGS) $(LIBS) -o join join.o table.o $(SISIS_API_C)

voter: voter.o table.o
	$(CC) $(CFLAGS) $(LIBS) -o voter voter.o table.o $(SISIS_API_C)

stop_redundancy: stop_redundancy.o
	$(CC) $(CFLAGS) $(LIBS) -o stop_redundancy stop_redundancy.o $(SISIS_API_C)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

