/*
 * task_handler.h
 *
 *  Created on: Nov 19, 2019
 *      Author: marzece
 */

#ifndef SRC_TASK_HANDLER_H_
#define SRC_TASK_HANDLER_H_
#include <stdlib.h>
#include "queue.h"
#include "fake_lwip.h"
#include <stdint.h>

extern struct tcp_pcb* data_client;
void main_loop();
Task* decide_on_task();
int perform_task(Task* task);
int readout_data(Task* task);
int readout_trigger(Task* task);
int send_data(Task* task);
err_t send_ack(void* arg, struct tcp_pcb* client, u16_t len);

#endif /* SRC_TASK_HANDLER_H_ */
