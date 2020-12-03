#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linenoise.h>

FILE* debug_file;
char* fpga_cli_hint_str = NULL;

static int command_fd = 0;
static int response_fd = 0;
static const int BUFFER_SIZE = 1024;
char response_buffer[BUFFER_SIZE];
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn);

typedef struct ServerCommand {
    const char* name;
    int nargs;
} ServerCommand;

static ServerCommand commandTable[] = {
    {"write_addr", 2},
    {"read_addr", 1},
    {"read_iic_reg", 1},
    {"write_iic_reg", 2},
    {"read_iic_bus", 1},
    {"write_iic_bus", 2},
    {"read_iic_bus_with_reg", 2},
    {"write_iic_bus_with_reg", 3},
    {"read_gpio0", 1},
    {"write_gpio0", 2},
    {"read_gpio1", 1},
    {"write_gpio1", 2},
    {"read_gpio2", 1},
    {"write_gpio2", 2},
    {"set_preamp_power", 1},
    {"set_adc_power", 1},
    {"read_ads_a", 1},
    {"write_ads_a", 2},
    {"read_ads_b", 1},
    {"write_ads_b", 2},
    {"write_a_adc_spi", 3},
    {"write_b_adc_spi", 3},
    {"ads_a_spi_data_available", 0},
    {"ads_b_spi_data_available", 0},
    {"ads_a_spi_pop", 0},
    {"ads_b_spi_pop", 0},
    {"write_lmk_if", 2},
    {"read_lmk_if", 1},
    {"write_lmk_spi", 3},
    {"lmk_spi_data_available", 0},
    {"lmk_spi_data_pop", 0},
    {"read_dac_if", 1},
    {"write_dac_if", 2},
    {"write_dac_spi", 3},
    {"toggle_dac_ldac", 0},
    {"toggle_dac_reset", 0},
    {"sleep", 1},
    {"", 0} // Must be last
};

void completion(const char *buf, linenoiseCompletions *lc) {
    // does buf have a null terminator? lets assume so lol
    int i = 0;
    ServerCommand* command = &(commandTable[0]);
    int buf_nchars = strlen(buf);
    while(strlen(command->name) > 0) {
        if(strnstr(command->name, buf, buf_nchars) != 0) {
            linenoiseAddCompletion(lc, command->name);
        }
        command = &(commandTable[++i]);
    }
}

void free_malloced_hint() {
    free(fpga_cli_hint_str);
    fpga_cli_hint_str = NULL;
}

char *hints(const char *buf, int *color, int *bold) {
    const char* arg_str = "arg";
    const int BUF_LEN = 256;
    int i = 0;
    int j;
    unsigned int chars_added = 0;
    ServerCommand* command = &(commandTable[0]);
    while(strlen(command->name) > 0) {
        if (!strcasecmp(buf,command->name)) {
            fpga_cli_hint_str = (char*) malloc(sizeof(char)*BUF_LEN);
            *color = 35;
            *bold = 0;
            for(j=0; j < command->nargs; j++) {
                snprintf(fpga_cli_hint_str+chars_added, BUF_LEN-chars_added, " %s%i", arg_str, j);
                chars_added += strlen(arg_str)+2;
            }
            return fpga_cli_hint_str;
        }
        command = &(commandTable[++i]);
    }
    return NULL;
}

int handle_line(const char* line) {
    size_t bytes_sent = 0;
    int bytes_recvd;
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return 0;
    }
    // Write the given line + NUL terminator
    write(command_fd, line, strlen(line)+1);

    read(response_fd, response_buffer, BUFFER_SIZE),

    printf("%s", response_buffer);
    return 0;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
    

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else if (!strcmp(*argv,"--keycodes")) {
            linenoisePrintKeyCodes();
            exit(0);
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes]\n", prgname);
            exit(1);
        }
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);
    linenoiseSetFreeHintsCallback(free_malloced_hint);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad("history.txt"); /* Load the history at startup */
    const char* prompt_string = "kintex> ";

    const char* command_fn = "kintex_command_pipe";
    const char* resp_fn = "kintex_response_pipe";

    command_fd = open(command_fn, O_WRONLY);
    if(command_fd <= 0) {
        printf("Error ocurred opening command pipe\n");
        printf("%s\n", strerror(errno));
        return 1;
    }
    response_fd = open(resp_fn, O_RDONLY);
    if(response_fd <= 0) {
        printf("Error ocurred opening response pipe\n");
        return 1;
    }

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */
    while((line = linenoise(prompt_string)) != NULL) {
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            handle_line(line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave("history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        free(line);
    }
    return 0;
}
