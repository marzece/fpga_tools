#ifndef __SERVER_COMMON__
#define __SERVER_COMMON__
#include <stdint.h>
#include "sds.h"


// TODO compiler complains about forward declaration figure out what the correct way to do this is
typedef struct client client; // Forward declaration
// Squelch warnings for unused arguements
#define UNUSED(V) ((void) V)

typedef void (*CLIFunc)(client* c, int argc, sds* argv);
typedef uint32_t (*LegacyFunc)(uint32_t* args);
typedef struct ServerCommand {
    char* name;
    CLIFunc func;
    LegacyFunc legacy_func;
    int nargs;
    int nresp;
    long long microseconds; // cumalative time spend executing this command
    long long calls; // cumalitive number of calls
} ServerCommand;

#endif
