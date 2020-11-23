
all: fnetctrl

CFLAGS += -std=gnu99 -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes

LDLIBS += -lm

fnetctrl: fnetctrl.o fnet_client.o

fnetctrl.o fnet_client.o: fakernet.h fnet_client.h

clean:
	rm -f *.o
	rm -f fnetctrl
