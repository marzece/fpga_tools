all: data_builder

data_builder: data_builder.c
	$(CC) -Wall -pedantic -O -g -o $@ $^ hiredis/libhiredis.a

clean:
	rm -f data_builder
