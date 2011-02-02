all:
	gcc -o remote_spawn -lrt -lpthread *.c ../tests/sisis_*.c

clean:
	rm remote_spawn
