all:
	gcc -o memory_monitor -lrt -lpthread *.c ../tests/sisis_*.c

clean:
	rm memory_monitor
