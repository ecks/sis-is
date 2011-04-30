EXECUTABLES = shim sort join voter
USER_OBJS   = shim.o voter.o sort.o join.o table.o

all: $(EXECUTABLES)

#sort: $(USER_OBJS)
#	$(CC) $(CFLAGS) -o sort $(USER_OBJS)

shim: shim.o table.o
	$(CC) $(CFLAGS) -o shim shim.o table.o

sort: sort.o table.o
	$(CC) $(CFLAGS) -o sort sort.o table.o

join: $(USER_OBJS)
	$(CC) $(CFLAGS) -o sort $(USER_OBJS)

voter: voter.o table.o
	$(CC) $(CFLAGS) -o voter voter.o table.o

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

