all:
	gcc -o ./test1 -lrt -lpthread *.c

clean:
	rm test1