#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fnet_client.h"
#include "iic.h"
#include "gpio.h"
#include "ads_if.h"
#include "lmk_if.h"
#include <linenoise.h>

// For doing "double" reads the 2nd read should be from this register
#define SAFE_READ_ADDRESS 0x0

struct fnet_ctrl_client* fnet_client;
FILE* debug_file;
char* fpga_cli_hint_str = NULL;

typedef uint32_t (*CLIFunc)(uint32_t* args);
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn);

typedef struct ServerCommand {
    char* name;
    CLIFunc func;
    int nargs;
} ServerCommand;

int setup_udp() {
    debug_file = fopen("fakernet_debug_log.txt", "r");
    const char* fnet_hname = "192.168.1.192";
    int reliable = 0; // wtf does this do?
    const char* err_string = NULL;
    fnet_client = fnet_ctrl_connect(fnet_hname, reliable, &err_string, debug_file);
    if(!fnet_client) {
        printf("ERROR Connecting!\n");
        return -1;
    }
    return 0;
}

uint32_t read_addr(uint32_t base, uint32_t addr) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      addr = base + addr;
      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(0x0);
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_READ | addr);

      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(fnet_client, num_items);
      
      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          printf("%s\n", fnet_ctrl_last_error(fnet_client));
          printf("%s\n", strerror(errno));
          return -1;
      }
      return ntohl(recv[0].data);
}

// My M_AXI Reg Mem IF has a 1-read latency.
// I.e. For each read request it returns the read result from the previous read.
// So by doing a "double" read I can get around this problem, but this kinda assumes reading
// is idempotent
// A better solution would be to have a super-secret address that says just return
// whatever the most recent read result was. Then I do a read from the desired address,
// then I read from that super-secret address
uint32_t double_read_addr(uint32_t base, uint32_t addr) {
    read_addr(base, addr);
    usleep(100);
    return read_addr(SAFE_READ_ADDRESS, 0x0);
}

int write_addr(uint32_t base, uint32_t addr, uint32_t data) {
      fakernet_reg_acc_item *send;
      fakernet_reg_acc_item *recv;

      addr = addr+base;
      fnet_ctrl_get_send_recv_bufs(fnet_client, &send, &recv);

      addr &= 0x3FFFFFF; // Only the first 25-bits are valid

      send[0].data = htonl(data);
      send[0].addr = htonl(FAKERNET_REG_ACCESS_ADDR_WRITE | addr);

      int num_items = 1;
      int ret = fnet_ctrl_send_recv_regacc(fnet_client, num_items);
      
      // Pretty sure "ret" will be the number of UDP reg-accs dones
      if(ret == 0) {
          printf("ERROR %i\n", ret);
          printf("%s\n", fnet_ctrl_last_error(fnet_client));
          printf("%s\n", strerror(errno));
          return -1;
      }
      return 0;
}

uint32_t write_addr_command(uint32_t* args) {
    return write_addr(args[0], 0, args[1]);
}

uint32_t read_addr_command(uint32_t* args) {
    return double_read_addr(args[0], 0);
}

uint32_t sleep_command(uint32_t* args) {
    return sleep(args[0]);
}

static ServerCommand commandTable[] = {
    {"write_addr", write_addr_command, 2},
    {"read_addr", read_addr_command, 1},
    {"read_iic_reg", read_iic_block_command, 1},
    {"write_iic_reg", write_iic_block_command, 2},
    {"read_iic_bus", read_iic_bus_command, 1},
    {"write_iic_bus", write_iic_bus_command, 2},
    {"read_iic_bus_with_reg", read_iic_bus_with_reg_command, 2},
    {"write_iic_bus_with_reg", write_iic_bus_with_reg_command, 3},
    {"read_gpio0", read_gpio_0_command, 1},
    {"write_gpio0", write_gpio_0_command, 2},
    {"read_gpio1", read_gpio_1_command, 1},
    {"write_gpio1", write_gpio_1_command, 2},
    {"read_gpio2", read_gpio_2_command, 1},
    {"write_gpio2", write_gpio_2_command, 2},
    {"set_preamp_power", set_preamp_power_command, 1},
    {"set_adc_power", set_adc_power_command, 1},
    {"read_ads_a", read_ads_a_if_command, 1},
    {"write_ads_a", write_ads_a_if_command, 2},
    {"read_ads_b", read_ads_b_if_command, 1},
    {"write_ads_b", write_ads_b_if_command, 2},
    {"write_a_adc_spi", write_a_adc_spi_command, 3},
    {"write_b_adc_spi", write_b_adc_spi_command, 3},
    {"ads_a_spi_data_available", ads_a_spi_data_available_command, 0},
    {"ads_b_spi_data_available", ads_b_spi_data_available_command, 0},
    {"ads_a_spi_pop", ads_a_spi_data_pop_command, 0},
    {"ads_b_spi_pop", ads_b_spi_data_pop_command, 0},
    {"write_lmk_if", write_lmk_if_command, 2},
    {"read_lmk_if", read_lmk_if_command, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0},
    {"sleep", sleep_command, 1},
    {"", NULL, 0} // Must be last
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

    char* command_name = NULL;
    char* arg_buff[11]= {command_name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    char* line_copy = malloc(sizeof(char)*(strlen(line)+1));
    strcpy(line_copy, line);
    command_name = strtok(line_copy, " \t\r\n"); // TODO include all WS characters
    arg_buff[0] = command_name;
    int nargs = 0;
    while(nargs-1 < 10) {
        arg_buff[nargs+1] = strtok(NULL, " \t\r\n");
        // TODO add empty string check
        if( arg_buff[nargs+1] == NULL) {
            break;
        }
        nargs +=1;
    }

    int cmd_index =0;
    ServerCommand* command = NULL;
    while(commandTable[cmd_index].func != NULL) {
        if(strcmp(commandTable[cmd_index].name, command_name) == 0) {
            command = &(commandTable[cmd_index]);
            break;
        }
        cmd_index +=1;
    }
    if(command->nargs != nargs) {
        // Too few arguments
        // TODO send back error message
        printf("Err: Command \"%s\" requires %i arguments, %i given.\n",command_name, command->nargs, nargs);
        return 0;
    }

    int i;
    uint32_t *args = malloc(sizeof(uint32_t)*nargs);
    for(i=0; i< nargs; i++) {
        args[i] = strtoul(arg_buff[i+1], NULL, 0);
    }

    uint32_t ret = command->func(args);
    printf("0x%x\n", ret);
    free(args);
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

    // Now connect to FPGA
    if(setup_udp()) {
        printf("error ocurred connecting to fpga\n");
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
