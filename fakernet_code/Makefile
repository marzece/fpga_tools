CC = cc

CFLAGS += -std=gnu99 -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes

LDLIBS += -lm

all: fnetctrl kintex_client_server kintex_cli fakernet_data_builder

fnetctrl: fnetctrl.o fnet_client.o

fnetctrl.o fnet_client.o: fakernet.h fnet_client.h

kintex_client_server: kintex_client_server.o hermes_if.o gpio.o lmk_if.o ads_if.o iic.o fnet_client.o dac_if.o axi_qspi.o ti_board_if.o jesd.o adc_sample_fanout.o channel_trigger.o data_pipeline.o server.o ae.o
	$(CC) -g -o $@ $(CFLAGS) $^

kintex_cli: kintex_cli.o
	$(CC) -g -o $@ $(CFLAGS) -Ilinenoise/ linenoise/linenoise.c $^

fakernet_data_builder: fakernet_data_builder.c crc32.o crc8.o
	$(CC) -Wall -pedantic -O -g -o $@ $^ fnet_client.o hiredis/libhiredis.a
#$(CC) -Wall -pedantic -O -g -o $@ $^ 

kintex_client_server.o: kintex_client_server.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

kintex_cli.o:kintex_cli.c
	$(CC) -g -o $@ -c $(CFLAGS) -Ilinenoise/ $^

hermes_if.o: hermes_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

ti_board_if.o: ti_board_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

gpio.o: gpio.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

lmk_if.o: lmk_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

ads_if.o: ads_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

iic.o: iic.c
	$(CC) -g -o $@ -c  $(CFLAGS) $^

dac_if.o: dac_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

axi_qspi.o: axi_qspi.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

jesd.o: jesd.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

adc_sample_fanout.o: adc_sample_fanout.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

channel_trigger.o: channel_trigger.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

data_pipeline.o: data_pipeline.c
	$(CC) -g -o $@ -c $(CFLAGS) $^


crc32.o: crc32.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

crc8.o: crc8.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

server.o: server.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

ae.o: ae.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

clean:
	rm -f *.o
	rm -f fnetctrl
	rm -f kintex_client_server
	rm -f kinex_cli
