/*
 * my_interrupt_handler.c
 *
 *  Created on: Nov 18, 2019
 *      Author: marzece
 */
#include "xparameters.h"
#include "xintc.h"
#include "xtmrctr_l.h"
#include "xil_io.h"
#include "queue.h"


void
trigger_fifo_interrupt_handler(void *p)
{
	xil_printf("INTERRUPT TESTTHING\n");

	// I think all I want to do here is push to the queue a readout task
	// (if none exists in the queue already?)
	Task* new_task = task_pool_alloc();
	new_task->id = READOUT_TRIGGER;
	push_to_queue(&task_list.trigger_readout_queue, new_task);
	// Clear the ISR
	Xil_Out32(XPAR_AXI_FIFO_MM_S_1_BASEADDR, 0xFFFFFFFF);

	// Acknowledge the interrupt
	XIntc_AckIntr(XPAR_INTC_0_BASEADDR, XPAR_AXI_FIFO_MM_S_1_INTERRUPT_MASK);
}

void trigger_fifo_setup_interrupts(XIntc* intcp) {
	const uint32_t trigger_fifo_addr = XPAR_AXI_FIFO_MM_S_1_BASEADDR;
	// See PG080 of Xilinx Documentation for AXI-Stream FIFO for origins of these numbers
	const uint32_t trigger_fifo_ier_addr = 0x4;
	const uint32_t trigger_fifo_ier_val = 0x4000000; // Only interrupt on receiving a packet of data
	xil_printf("Setting up fifo interrupts\n");
	Xil_Out32(trigger_fifo_addr + trigger_fifo_ier_addr, trigger_fifo_ier_val);

	// The last argument is the argument to be given to the call back, none needed I think,
	// but it could be a pointer to some memory that gets update as needed
	// Maybe it should be a pointer to some struct that is used in the readout?
	xil_printf("%i\n", XPAR_MICROBLAZE_0_AXI_INTC_AXI_FIFO_MM_S_1_INTERRUPT_INTR);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_MICROBLAZE_0_AXI_INTC_AXI_FIFO_MM_S_1_INTERRUPT_INTR,
			             (XInterruptHandler)trigger_fifo_interrupt_handler, NULL);
	XIntc_Enable(intcp, XPAR_MICROBLAZE_0_AXI_INTC_AXI_FIFO_MM_S_1_INTERRUPT_INTR);
}
