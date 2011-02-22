all:
	gcc -o machine_monitor -lrt -lpthread *.c ../tests/sisis_*.c

clean:
	rm machine_monitor
