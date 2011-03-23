all:
	gcc -o ./test1 -lrt -lpthread test1.c sisis_*.c

clean:
	rm test1