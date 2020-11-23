all: main full_test fifo_recvr full_test fake_data_builder

tests: task_pool_test task_queue_test data_buffer_test task_handler_test

fake_data_builder: fake_data_builder.o hiredis/libhiredis.a
	cc -Wall -g -o $@ $^

fake_data_builder.o: fake_data_builder.c
	cc -g -Wall -o $@ -c $^

task_handler_test: task_handler_test.o queue.o data_buffer_v2.o fake_lwip.o task_handler.o
	cc -Wall -g -o $@ $^

task_handler_test.o: task_handler_test.c
	cc -g -Wall -o $@ -c $^

task_pool_test: task_pool_test.o queue.o
	cc -Wall -g -o $@ $^

task_pool_test.o: task_pool_test.c
	cc -g -Wall -o $@ -c $^

task_queue_test: task_queue_test.o queue.o
	cc -Wall -g -o $@ $^

task_queue_test.o: task_queue_test.c
	cc -g -Wall -o $@ -c $^

data_buffer_test: data_buffer_test.o data_buffer_v2.o
	cc -Wall -O -g -o $@ $^

data_buffer_test.o: data_buffer_test.c
	cc -g -O -Wall -o $@ -c $^

main: main.o
	cc -g -o $@ $^

main.o: main.c
	cc -g -c -o $@ $^

fifo_recvr: fifo_recvr.c
	cc -g -o $@ $^

full_test: full_test.o queue.o data_buffer_v2.o task_handler.o fake_lwip.o
	cc -g -o $@ $^

full_test.o: full_test.c
	cc -g -c -o $@ $^

queue.o: queue.c
	cc -g -c -o $@ $^

data_buffer.o: data_buffer.c
	cc -g -c -o $@ $^

task_handler.o: task_handler.c
	cc -g -c -o $@ $^

fake_lwip.o: fake_lwip.c
	cc -g -c -o $@ $^

clean:
	rm -rf *.o *.dSYM main full_test fifo_recvr
