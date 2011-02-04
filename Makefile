MYFLAGS=`pkg-config --cflags --libs cairo gtk+-2.0`

all:
	gcc ${MYFLAGS} -ggdb -lrt -lpthread -o ./vis vis_main.c ../tests/sisis_api.c ../tests/sisis_netlink.c vis_window.c

clean:
	rm vis
