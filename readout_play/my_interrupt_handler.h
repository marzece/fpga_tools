/*
 * my_interrupt_handler.h
 *
 *  Created on: Nov 18, 2019
 *      Author: marzece
 */

#ifndef SRC_MY_INTERRUPT_HANDLER_H_
#define SRC_MY_INTERRUPT_HANDLER_H_

#include "xintc.h"
void trigger_fifo_interrupt_handler(void* p);
void trigger_fifo_setup_interrupts(XIntc* intcp);
#endif /* SRC_MY_INTERRUPT_HANDLER_H_ */
