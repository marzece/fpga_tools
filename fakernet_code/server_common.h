#ifndef __SERVER_COMMON__
#define __SERVER_COMMON__
#include <stdint.h>
typedef uint32_t (*CLIFunc)(uint32_t* args);

typedef struct ServerCommand {
    const char* name;
    CLIFunc func;
    int nargs;
} ServerCommand;
#endif
