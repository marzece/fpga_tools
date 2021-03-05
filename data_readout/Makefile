all: data_builder fakernet_data_builder

data_builder: data_builder.c
	$(CC) -Wall -pedantic -O -g -o $@ $^ hiredis/libhiredis.a

fakernet_data_builder: fakernet_data_builder.c
	$(CC) -Wall -pedantic -O -g -o $@ $^ hiredis/libhiredis.a

clean:
	rm -f data_builder fakernet_data_builder
