MYCFLAGS=`pkg-config --cflags cairo`
MYLDFLAGS=`pkg-config --libs cairo`

all:
	gcc ${MYCFLAGS} ${MYLDFLAGS} -ggdb -lrt -lpthread -o ./vis vis_main.c ../tests/sisis_api.c ../tests/sisis_netlink.c vis_window.c

clean:
	rm vis
