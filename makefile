all:
	gcc -o leader_elector -lrt -lpthread *.c ../tests/sisis_*.c

clean:
	rm leader_elector
