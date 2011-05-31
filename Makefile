CC = gcc
EXECUTABLES = shim sort sortv2 join voter stop_redundancy visualization_feed demo_killer
SISIS_API_C = ../tests/sisis_*.c
LIBS = -lrt -lpthread

all: $(EXECUTABLES)

shim: shim.o table.o demo.o
	$(CC) $(CFLAGS) $(LIBS) -o shim shim.o table.o demo.o $(SISIS_API_C)

sort: sort.o table.o redundancy.o demo.o
	$(CC) $(CFLAGS) $(LIBS) -o sort sort.o table.o redundancy.o demo.o $(SISIS_API_C)

sortv2: sortv2.o table_bubblesort.o redundancy.o demo.o
	$(CC) $(CFLAGS) $(LIBS) -o sortv2 sortv2.o table_bubblesort.o redundancy.o demo.o $(SISIS_API_C)

sortv2.o:
	gcc -DBUBBLE_SORT -o sortv2.o -c sort.c

table_bubblesort.o:
	gcc -DBUBBLE_SORT -o table_bubblesort.o -c table.c

join: join.o table.o redundancy.o demo.o
	$(CC) $(CFLAGS) $(LIBS) -o join join.o table.o redundancy.o demo.o $(SISIS_API_C)

voter: voter.o table.o redundancy.o demo.o
	$(CC) $(CFLAGS) $(LIBS) -o voter voter.o table.o redundancy.o demo.o $(SISIS_API_C)

stop_redundancy: stop_redundancy.o
	$(CC) $(CFLAGS) $(LIBS) -o stop_redundancy stop_redundancy.o $(SISIS_API_C)

visualization_feed: visualization_feed.o
	$(CC) $(CFLAGS) $(LIBS) -o visualization_feed visualization_feed.o $(SISIS_API_C)

demo_killer: killer.o
	$(CC) $(CFLAGS) $(LIBS) -o demo_killer killer.o $(SISIS_API_C)

.c.o: 
	gcc -c $*.c

clean:
	/bin/rm -f *.o core $(EXECUTABLES) 

