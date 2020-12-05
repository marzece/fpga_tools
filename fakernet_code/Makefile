

CC = cc

CFLAGS += -std=gnu99 -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes

LDLIBS += -lm

all: fnetctrl kintex_client_server kintex_cli

fnetctrl: fnetctrl.o fnet_client.o

fnetctrl.o fnet_client.o: fakernet.h fnet_client.h

kintex_client_server: kintex_client_server.o gpio.o lmk_if.o ads_if.o iic.o fnet_client.o dac_if.o axi_qspi.o
	$(CC) -o $@ $(CFLAGS) $^

kintex_cli: kintex_cli.o
	$(CC) -o $@ $(CFLAGS) -Ilinenoise/ linenoise/linenoise.c $^

kintex_client_server.o: kintex_client_server.c
	$(CC) -o $@ -c $(CFLAGS) $^

kintex_cli.o:kintex_cli.c
	$(CC) -o $@ -c $(CFLAGS) -Ilinenoise/ $^

gpio.o: gpio.c
	$(CC) -o $@ -c $(CFLAGS) $^

lmk_if.o: lmk_if.c
	$(CC) -o $@ -c $(CFLAGS) $^

ads_if.o: ads_if.c
	$(CC) -o $@ -c $(CFLAGS) $^

iic.o: iic.c
	$(CC) -o $@ -c  $(CFLAGS) $^

dac_if.o: dac_if.c
	$(CC) -o $@ -c $(CFLAGS) $^

axi_qspi.o: axi_qspi.c
	$(CC) -o $@ -c $(CFLAGS) $^

clean:
	rm -f *.o
	rm -f fnetctrl
	rm -f kintex_client_server
	rm -f kinex_cli
