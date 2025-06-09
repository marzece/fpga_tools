CC = cc

#CFLAGS += -O0 -g -std=gnu99 -fsanitize=address -fno-omit-frame-pointer -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes
CFLAGS += -O0 -g -std=gnu99 -fno-omit-frame-pointer -Wall -W -Wshadow -Wwrite-strings \
        -Wno-unused-function -Wno-unused-label -Wstrict-prototypes

LDLIBS += -lm

# Comment/uncomment below if you want the data builder to dump data directly
#DUMP_DATA=-DDUMP_DATA
DUMP_DATA=


all: fnetctrl kintex_client_server kintex_cli ceres_data_builder tail_daq_log fontus_data_builder zipper ceres_server fake_data_gen zookeeper

fnetctrl: fnetctrl.o fnet_client.o
	$(CC) -o $@ $(CFLAGS) $^ -lm

zipper: zipper.c hiredis/libhiredis.a util.o daq_logger.o
	$(CC) -O0 -o $@ $(CFLAGS) $^

tail_daq_log: tail_daq_log.o hiredis/libhiredis.a
	$(CC) -o $@ $(CFLAGS) $^

kintex_client_server: kintex_client_server.o gpio.o lmk_if.o ads_if.o iic.o fnet_client.o dac_if.o axi_qspi.o jesd.o jesd_phy.o channel_trigger.o data_pipeline.o ceres_if.o reset_gen_if.o server.o ae.o blocked.o sds.o adlist.o connection.o anet.o networking.o util.o trigger_pipeline.o fontus_if.o clock_wiz.o daq_logger.o hiredis/libhiredis.a
	$(CC) -o $@ $(CFLAGS) $^

ceres_server: ceres_server.o gpio.o lmk_if.o ads_if.o iic.o fnet_client.o dac_if.o axi_qspi.o jesd.o jesd_phy.o channel_trigger.o data_pipeline.o ceres_if.o reset_gen_if.o server.o ae.o blocked.o sds.o adlist.o connection.o anet.o networking.o util.o trigger_pipeline.o clock_wiz.o daq_logger.o hiredis/libhiredis.a
	$(CC) -o $@ $(CFLAGS) $^

zookeeper: zookeeper.c data_builder.o crc32.o crc8.o fnet_client.o daq_logger.o server.o networking.o util.o connection.o sds.o ae.o blocked.o adlist.o anet.o hiredis/libhiredis.a
	$(CC) -o $@ $(CFLAGS) $^

kintex_cli: kintex_cli.o
	$(CC) -o $@ $(CFLAGS) -Ilinenoise/ linenoise/linenoise.c $^

fontus_data_builder: fakernet_data_builder.c data_builder.o crc32.o crc8.o daq_logger.o
	$(CC) -Wall $(CFLAGS) -O0 -o $@ $^ fnet_client.o hiredis/libhiredis.a -DFONTUS=1 $(DUMP_DATA)

ceres_data_builder: fakernet_data_builder.c data_builder.o crc32.o crc8.o daq_logger.o
	$(CC) -Wall $(CFLAGS) -O0 -o $@ $^ fnet_client.o hiredis/libhiredis.a -DCERES=1 $(DUMP_DATA)

data_builder.o: data_builder.c
	$(CC) -o $@ -c $(CFLAGS) $^

fake_data_gen: fake_data_gen.c crc32.o crc8.o
	$(CC) -g -o $@ $(CFLAGS) $^

kintex_client_server.o: kintex_client_server.c
	$(CC) -o $@ -c $(CFLAGS) $^

ceres_server.o: ceres_server.c
	$(CC) -o $@ -c $(CFLAGS) $^

kintex_cli.o:kintex_cli.c
	$(CC) -o $@ -c $(CFLAGS) -Ilinenoise/ $^

tail_daq_log.o: tail_daq_log.c
	$(CC) -o $@ -c $(CFLAGS) $^

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

jesd.o: jesd.c
	$(CC) -o $@ -c $(CFLAGS) $^

reset_gen_if.o: reset_gen_if.c
	$(CC) -o $@ -c $(CFLAGS) $^

channel_trigger.o: channel_trigger.c
	$(CC) -o $@ -c $(CFLAGS) $^

data_pipeline.o: data_pipeline.c
	$(CC) -o $@ -c $(CFLAGS) $^

clock_wiz.o: clock_wiz.c
	$(CC) -o $@ -c $(CFLAGS) $^

trigger_pipeline.o: trigger_pipeline.c
	$(CC) -o $@ -c $(CFLAGS) $^


ceres_if.o: ceres_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

fontus_if.o: fontus_if.c
	$(CC) -g -o $@ -c $(CFLAGS) $^

crc32.o: crc32.c
	$(CC) -o $@ -c $(CFLAGS) $^

crc8.o: crc8.c
	$(CC) -o $@ -c $(CFLAGS) $^

server.o: server.c
	$(CC) -o $@ -c $(CFLAGS) $^

ae.o: ae.c
	$(CC) -o $@ -c $(CFLAGS) $^

blocked.o: blocked.c
	$(CC) -o $@ -c $(CFLAGS) $^

connection.o: connection.c
	$(CC) -o $@ -c $(CFLAGS) $^

sds.o: sds.c
	$(CC) -o $@ -c $(CFLAGS) $^

adlist.o: adlist.c
	$(CC) -o $@ -c $(CFLAGS) $^

anet.o: anet.c
	$(CC) -o $@ -c $(CFLAGS) $^

networking.o: networking.c
	$(CC) -o $@ -c $(CFLAGS) $^

jesd_phy.o: jesd_phy.c
	$(CC) -o $@ -c $(CFLAGS) $^

util.o: util.c
	$(CC) -o $@ -c $(CFLAGS) $^

daq_logger.o: daq_logger.c
	$(CC) -o $@ -c $(CFLAGS) $^

fnet_client.o: fnet_client.c
	$(CC) -o $@ -c $(CFLAGS) $^

fnetctrl.o: fnetctrl.c
	$(CC) -o $@ -c $(CFLAGS) $^

clean:
	rm -f *.o fnetctrl kintex_client_server kintex_cli fakernet_data_builder tail_daq_log fontus_data_builder zipper ceres_server
