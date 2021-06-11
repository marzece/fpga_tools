#ifndef __CERES_IF__
#define  __CERES_IF__
#include "server_common.h"
#include <stdlib.h>
#include <stdint.h>

// TODO I need some way to link between IIC Addresses and R/W sizes for each chip
extern ServerCommand ceres_commands[];
extern const uint32_t CERES_SAFE_READ_ADDRESS;

#endif
