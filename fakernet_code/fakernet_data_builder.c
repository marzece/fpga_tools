/*
Author: Eric Marzec <marzece@gmail.com>
    This program is just the "main" for the data builder. All the actual code
    is in 'data_builder.c'.
    It can be compiled to run as either the data builder for FONTUS data or
    for CERES data.
*/

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "data_builder.h"

// Prints help string which describes this programs CL args.
void print_help_message(void) {
    struct BuilderConfig cfg_default = default_builder_config();
#if CERES
    const char* program_string = "ceres_data_builder";
    const char* board_string = "CERES";
#else // FONTUS
    const char* program_string = "fontus_data_builder";
    const char* board_string = "FONTUS";
#endif

    printf("%s: recieves then combines data from a %s board and publishes it to redis and/or saves it to a file.\n"
            "\tusage:  %s [--ip fpga-ip] [-o output-filename] [--no-save] [-n num-events] [--dry] [-v] [-q]\n"
            "\targuments:\n"
            "\t--ip -i\tFPGA IP address to recieve data from. Default is '%s'\n"
            "\t--out -o\tFile to write built data to. Default is '%s'\n"
            "\t--num-events -n\tExit after N events are built. Default is 0, which corresponds to no limit.\n"
            "\t--dry-run -d\tExit after N events are built. Default is 0, which corresponds to no limit.\n"
            "\t--no-save\tDo not save any data to a file. Events are still published to redis.\n"
            "\t--log-file -l\tFilename that log messages should be recorded to. Default '%s'\n"
            "\t--redis-host -r\tHostname for redis DB. Used for publishing data & monitoring stats. Default is '%s'\n"
            "\t--verbose -v\tIncrease verbosity. Can be done multiple times.\n"
            "\t--quiet -q\tDecrease verbosity. Can be done multiple times.\n"
            "\t--help -h\tDisplay this message\n",
            program_string, board_string, program_string,
          cfg_default.ip, cfg_default.output_filename, cfg_default.error_filename,
          cfg_default.redis_host);
}

// Populate configuration from CL args
struct BuilderConfig make_config_from_args(int argc, char** argv) {
    struct option clargs[] = {
        {"out", required_argument, NULL, 'o'},
        {"ip", required_argument, NULL, 'i'},
        {"num-events", required_argument, NULL, 'n'},
        {"dry-run", no_argument, NULL, 'd'},
        {"no-save", no_argument, NULL, 's'},
        {"log-file", required_argument, NULL, 'l'},
        {"redis-host", required_argument, NULL, 'r'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        { 0, 0, 0, 0}};
    int optindex;
    int opt;
    struct BuilderConfig config = default_builder_config();
    while(!config.exit_now &&
            ((opt = getopt_long(argc, argv, "o:i:n:r:l:dsvh", clargs, &optindex)) != -1)) {
        switch(opt) {
            case 0:
                // Should be here if the option (in 'clargs') has the "flag"
                // field set. Currently all are NULL in that field so should
                // never get here.
                break;
            case 'o':
                printf("Output data file set to '%s'\n", optarg);
                config.output_filename = optarg;
                break;
            case 'i':
                // FPGA IP Address
                config.ip = optarg;
                break;
            case 'n':
                config.num_events = strtoul(optarg, NULL, 0);
                break;
            case 'd':
                config.dry_run = 1;
                break;
            case 'l':
                printf("Log file set to %s\n", optarg);
                config.error_filename = optarg;
                break;
            case 's':
                config.do_not_save = 1;
                break;
            case 'r':
                config.redis_host = optarg;
                printf("Redis host set to '%s'\n", optarg);
                break;
            case 'v':
                // Reduce the threshold on all the verbosity levels
                config.verbosity += 1;
                break;
            case 'q':
                // Raise the threshold on all the verbosity levels
                config.verbosity -= 1;
                break;
            case 'h':
                print_help_message();
                config.exit_now = 1;
                break;
            case '?':
            case ':':
                // This should happen if there's a missing or unknown argument
                // getopt_long outputs its own error message
                config.exit_now = 1;
                break;
        }
    }
    return config;
}

int main(int argc, char **argv) {

    struct BuilderConfig config = make_config_from_args(argc, argv);
    if(config.exit_now) {
        return 0;
    }
#if FONTUS
    config.ceres_builder = 0;
#elif CERES
    config.ceres_builder = 1;
#else
    #error "Data builder must be compiled with either the FONTUS or CERES flag"
    return -1;
#endif

    data_builder_main(config);
    return 0;
}
