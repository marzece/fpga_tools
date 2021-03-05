#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int resp_uint32(char* buffer, int buff_max, uint32_t val) {
    // TODO check bytes written < buff_max!
    return snprintf(buffer, buff_max, ":%u\r\n", val);
}

int resp_array(char* buffer, int buff_max, uint32_t *val, unsigned int nitems) {
    unsigned int i;
    int bytes;

    bytes = snprintf(buffer, buff_max, "*%u\r\n", nitems);
    for(i=0; i<nitems; i++) {
        bytes += resp_uint32(buffer+bytes, buff_max - bytes, val[i]);
        if(bytes >= buff_max) {
            return bytes;
        }
    }
    return bytes;
}
