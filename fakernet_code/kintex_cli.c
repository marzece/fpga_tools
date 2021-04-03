#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linenoise.h>

#define BUFFER_SIZE 2048
#define SEND_COMMAND_TABLE_COMMAND "send_command_table"

FILE* debug_file;
char* fpga_cli_hint_str = NULL;

static int command_fd = 0;
static int response_fd = 0;
char response_buffer[BUFFER_SIZE];
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn);

typedef struct ServerCommand {
    char* name;
    int nargs;
} ServerCommand;
static ServerCommand* commandTable = NULL;

int grab_response(void) {
    int nbytes_read = read(response_fd, response_buffer, BUFFER_SIZE);
    if(nbytes_read >= BUFFER_SIZE-1) {
        printf("Response too long...can't handle this\n");
        exit(1);
    }

    // Can't expect the response to have a NULL terminator
    response_buffer[nbytes_read] = '\0';
    return nbytes_read;

}

void completion(const char *buf, linenoiseCompletions *lc) {
    // does buf have a null terminator? lets assume so lol
    int i = 0;
    if(!commandTable) {
        return;
    }

    ServerCommand* command = &(commandTable[0]);
    while(command->name && strlen(command->name) > 0) {
        if(strstr(command->name, buf) != 0) {
            linenoiseAddCompletion(lc, command->name);
        }
        command = &(commandTable[++i]);
    }
}

void free_malloced_hint(void* data) {
    (void) data; // UNUSED
    free(fpga_cli_hint_str);
    fpga_cli_hint_str = NULL;
}

char *hints(const char *buf, int *color, int *bold) {
    const char* arg_str = "arg";
    const int BUF_LEN = 256;
    int i = 0;
    int j;
    unsigned int chars_added = 0;
    if(!commandTable) {
        return NULL;
    }
    ServerCommand* command = &(commandTable[0]);
    while(command->name != NULL && strlen(command->name) > 0) {
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

void produce_command_table(char* table_string) {
    // TODO should just use hiredis to parse
    int i;
    char* tok = strtok(table_string, "\r\n");
    // The first token should be *xx\r\n where xx is the number of items in the command table
    if(tok[0] != '*') {
        printf("Error reading command table, tab-complete won't work\n");
        return;
    }
    int ncommands = atoi(tok+1);
    commandTable = malloc(sizeof(ServerCommand)*(ncommands +1));
    memset(commandTable, 0, sizeof(ServerCommand)*(ncommands + 1));

    char* location;
    for(i=0; i<ncommands; i++) {
        tok = strtok(NULL, "\r\n");
        if(tok[0] != '+') {
            printf("Command without a '+' found. Tab-completion won't work\n");
            goto CLEAR_COMMAND_TABLE;
        }
        tok = tok+1; //Skip past the '+'

        location = strstr(tok, " ");
        if(location==NULL) {
            printf("Command ill-formed. Tab-completion won't work\n");
            goto CLEAR_COMMAND_TABLE;
        }

        int nargs = atoi(location);
        int command_nbytes = (location - tok);
        commandTable[i].nargs = nargs;
        commandTable[i].name = malloc(command_nbytes + 1);
        memcpy(commandTable[i].name, tok, command_nbytes);
        commandTable[i].name[command_nbytes] = '\0';
    }
    commandTable[ncommands].name=NULL;
    commandTable[ncommands].nargs=0;
    printf("%i commands loaded from server\n", ncommands);
    return;
CLEAR_COMMAND_TABLE:;
    ServerCommand* command = commandTable;
    while(command->name != NULL) {
        free(command->name);
    }
    free(commandTable);
    commandTable = NULL;
}

int handle_line(const char* line) {
    // first check if the first char is a '#' or the line is empty
    // if it is, treat this as a comment
    if(strlen(line) == 0 || line[0] == '#') {
        return 0;
    }
    // Write the given line (without NULL terminator)
    write(command_fd, line, strlen(line));

    // Fills result into response_buffer
    int nbytes_read = grab_response();

    if(response_buffer[nbytes_read-1] != '\n') {
        printf("%s\n", response_buffer);
    }
    else {
        printf("%s", response_buffer);
    }
    return 0;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
    int batch = 0;
    

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
        } else if(!strcmp(*argv, "--batch")) {
            batch = 1;
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

    // First get the command table
    if(!batch) {
        write(command_fd, SEND_COMMAND_TABLE_COMMAND, strlen(SEND_COMMAND_TABLE_COMMAND));
        grab_response();
        produce_command_table(response_buffer);
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

    // TODO should clean up command table
    return 0;
}
