#ifndef  __DATA_BUILDER_H__
#define __DATA_BUILDER_H__
#include <stdint.h>


// Configuration parameters for running the data builder
struct BuilderConfig {
    int ceres_builder; // non-zero to build CERES events, 0 to build FONTUS events
    const char* ip; // FPGA IP address
    unsigned int num_events; // Number of events to build before exiting (0 means infinite)
    int dry_run; // Dry run, dummy mode, don't actually connect or do anything
    int do_not_save; // Don't save data to a file
    int verbosity; // Default is zero. Higher is lowder. Lower is quieter
    const char* output_filename; // File to write data to
    const char* error_filename; // File to write log messages to
    const char* redis_host; // Redis DB hostname, used for publishing data & stats
    int in_pipe;
    int out_pipe;
    int exit_now; // Exit the program. Mostly just used as a hack to stop the program from running if config isn't valid.
};

// Process IO interface
typedef struct MangerIO {
    int32_t command;
    int32_t arg;
} ManagerIO;

enum ManagerIOCommand {
    CMD_NONE=0,
    CMD_CONNECTED, // Returns 1 if connected to FPGA, 0 otherwise
    CMD_ISREELING, // Returns 1 if currently "reeling", 0 otherwise
    CMD_NUMBUILT,  // Returns the number of events built since the program started
};

struct BuilderConfig default_builder_config(void);
int data_builder_main(struct BuilderConfig config);
#endif
