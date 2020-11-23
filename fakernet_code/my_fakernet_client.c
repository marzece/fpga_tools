#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linenoise/linenoise.h"
#include "fnet_client.h"


typedef int (*CommandFunction)(uint32_t*);
struct fnet_ctrl_client* fnet_client;
void setup_udp();
int send_cmd(uint32_t* args);

typedef struct Command {
    const char* command;
    CommandFunction func;
    int nargs;
} Command;

static Command command_table[]  = {
    {"send_cmd", send_cmd, 0},
    {NULL, NULL, 0}
};

void setup_udp() {
    FILE* debug_file = fopen("fakernet_debug_log.txt", "r");
    const char* fnet_hname = "192.168.1.192";
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, debug_file);
}

int send_cmd(uint32_t* args) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      send[0].data = 0xFACEBEEF;
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | 0x69);
      int num_items = 1;

      if(fnet_ctrl_send_recv_regacc(fnet_client, num_items)) {
          printf("ERROR\n");
          return -1;
      }

      return 0;
}

int handle_line(const char* line) {
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return 0;
    }

    Command* this_command = command_table[0];
    while(this_command.command) {
        if(strcmp(this_command.command, line)) {

        }

    }


    return 0;
}


void completion(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == 'h') {
        linenoiseAddCompletion(lc,"hello");
        linenoiseAddCompletion(lc,"hello there");
    }
}

char *hints(const char *buf, int *color, int *bold) {
    if (!strcasecmp(buf,"hello")) {
        *color = 35;
        *bold = 0;
        return " World";
    }
    return NULL;
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

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad("history.txt"); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */
    
    setup_udp();

    while((line = linenoise("fnet> ")) != NULL) {
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            handle_line(line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave("history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (!strncmp(line, "/mask", 5)) {
            linenoiseMaskModeEnable();
        } else if (!strncmp(line, "/unmask", 7)) {
            linenoiseMaskModeDisable();
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        free(line);
    }
    return 0;
}
