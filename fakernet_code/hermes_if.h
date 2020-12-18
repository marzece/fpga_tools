#ifndef __HERMES_IF__
#define  __HERMES_IF__
#include "server_common.h"
#include <stdlib.h>
#include <stdint.h>


// TODO I need some way to link between IIC Addresses and R/W sizes for each chip

extern ServerCommand hermes_commands[];
extern const uint32_t HERMES_SAFE_READ_ADDRESS;

#endif
