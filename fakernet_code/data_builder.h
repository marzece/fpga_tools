#ifndef  __DATA_BUILDER_H__
#define __DATA_BUILDER_H__


// Configuration parameters for running the data builder
struct BuilderConfig {
    int ceres_builder; // non-zero to build CERES events, 0 to build FONTUS events
    const char* ip; // FPGA IP address
    unsigned int num_events; // Number of events to build before exiting
    int dry_run; // Dry run, dummy mode, don't actually connect or do anything
    int do_not_save; // Don't save data to a file
    int verbosity; // Default is zero. Higher is lowder. Lower is quieter
    const char* output_filename; // File to write data to
    const char* error_filename; // File to write log messages to
    const char* redis_host; // Redis DB hostname, used for publishing data & stats
    int exit_now; // Exit the program. Mostly just used as a hack to stop the program from running if config isn't valid.
};

struct BuilderConfig default_config(void);
int data_builder_main(struct BuilderConfig config);
#endif
