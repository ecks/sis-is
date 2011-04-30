EXECUTABLES = sort join
USER_OBJS   = sort.o join.o table.o

all: $(EXECUTABLES)

#sort: $(USER_OBJS)
#	$(CC) $(CFLAGS) -o sort $(USER_OBJS)

sort: sort.o table.o
	$(CC) $(CFLAGS) -o sort sort.o table.o

join: $(USER_OBJS)
	$(CC) $(CFLAGS) -o sort $(USER_OBJS)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

