all:
	gcc -o ./test1 -lpthread *.c

clean:
	rm test1