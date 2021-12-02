CC = cc

CFLAGS += -O0 -std=gnu99 -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes

LDLIBS += -lm

all: fnetctrl kintex_client_server kintex_cli fakernet_data_builder

fnetctrl: fnetctrl.o fnet_client.o

fnetctrl.o fnet_client.o: fakernet.h fnet_client.h

kintex_client_server: kintex_client_server.o hermes_if.o gpio.o lmk_if.o ads_if.o iic.o fnet_client.o dac_if.o axi_qspi.o ti_board_if.o jesd.o jesd_phy.o adc_sample_fanout.o channel_trigger.o data_pipeline.o ceres_if.o server.o ae.o sds.o adlist.o connection.o anet.o networking.o util.o daq_logger.o hiredis/libhiredis.a
	$(CC) -ggdb3 -o $@ $(CFLAGS) $^

kintex_cli: kintex_cli.o
	$(CC) -ggdb3 -o $@ $(CFLAGS) -Ilinenoise/ linenoise/linenoise.c $^

fakernet_data_builder: fakernet_data_builder.c crc32.o crc8.o daq_logger.o
	$(CC) -Wall $(CFLAGS) -O0 -ggdb3 -o $@ $^ fnet_client.o hiredis/libhiredis.a
#$(CC) -Wall -pedantic -O -g -o $@ $^ 

kintex_client_server.o: kintex_client_server.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

kintex_cli.o:kintex_cli.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) -Ilinenoise/ $^

hermes_if.o: hermes_if.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

ti_board_if.o: ti_board_if.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

gpio.o: gpio.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

lmk_if.o: lmk_if.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

ads_if.o: ads_if.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

iic.o: iic.c
	$(CC) -ggdb3 -o $@ -c  $(CFLAGS) $^

dac_if.o: dac_if.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

axi_qspi.o: axi_qspi.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

jesd.o: jesd.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

adc_sample_fanout.o: adc_sample_fanout.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

channel_trigger.o: channel_trigger.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

data_pipeline.o: data_pipeline.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

ceres_if.o: ceres_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

crc32.o: crc32.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

crc8.o: crc8.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

server.o: server.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

ae.o: ae.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

connection.o: connection.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

sds.o: sds.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

adlist.o: adlist.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

anet.o: anet.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

networking.o: networking.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

jesd_phy.o: jesd_phy.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

util.o: util.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

daq_logger.o: daq_logger.c
	$(CC) -ggdb3 -o $@ -c $(CFLAGS) $^

clean:
	rm -f *.o
	rm -f fnetctrl
	rm -f kintex_client_server
	rm -f kinex_cli
