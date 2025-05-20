/*
 * fake_data_gen.c
 * Author: Eric Marzec <marzece@umich.edu>
 * This program is designed to fake the data produced by CERES/FONTUS FPGAs.
 * The program will listen for TCP connections and once one is received it will
 * start sending data to it at a fixed rate. More connections can be received
 * and each will get the same fake data.
 *
 * TODO:
 * Currently if a client disconnects it's not fully removed from consideration.
 * This is because I track connected file-descriptors using a fixed length array,
 * so if someone disconnects I ought to "pop" that descriptor from the array.
 * But that "pop" operation is annoying to do with an array, a linked-list would
 * be more appropriate. So I should implement that linked-list strategy instead.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>

#define PORT "5009"
#define BACKLOG 10

typedef struct FontusTrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t trigger_flags;
    uint32_t self_trigger_word;
    uint64_t beam_trigger_time;
    uint64_t led_trigger_time;
    uint64_t ct_time;
    uint32_t crc;
} FontusTrigHeader;

uint32_t crc32(uint32_t crc, uint32_t * buf, unsigned int len);
void crc8(unsigned char *crc, unsigned char m);
#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define ntohll(x) htonll(x)

// get sockaddr, IPv4
void *get_in_addr(struct sockaddr *sa) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
}

// This function produce fake FONTUS data with a correct CRC value and then
// stuffs it in the given buffer ready to be sent out over the network.
static size_t produce_fontus_data(unsigned char* buffer, const int number) {
    static FontusTrigHeader header = {
     .magic_number = 0xF00FF00F,
     .trig_number = 0,
     .clock = 0,
     .length = 0,
     .device_number = 0,
     .trigger_flags = 0,
     .self_trigger_word = 0,
     .beam_trigger_time = 0,
     .led_trigger_time = 0,
     .ct_time = 0,
     .crc =0};

    header.trig_number = number;

    // Magic Number
    *((uint32_t*)buffer) = htonl(header.magic_number);
    buffer += 4;

    // Mark this as the "start" of the header in the buffer,
    // we don't need to revisit the magic number b/c it's not used in the
    // CRC calculation
    unsigned char* start = buffer;

    // Trigger Number
    *((uint32_t*)buffer) = htonl(header.trig_number);
    buffer += 4;

    // Clock
    *((uint64_t*)buffer) = htonll(header.clock);
    buffer += 8;

    // Length Parameter
    *((uint16_t*)buffer) = htons(header.length);
    buffer += 2;

    // Device ID
    *((uint8_t*)buffer) = header.device_number;
    buffer += 1;

    // Trigger Flags
    *((uint8_t*)buffer) = header.trigger_flags;
    buffer += 1;

    // Self trigger word
    *((uint32_t*)buffer) = htonl(header.self_trigger_word);
    buffer += 4;

    // beam trigger time
    *((uint64_t*)buffer) = htonll(header.beam_trigger_time);
    buffer += 8;

    // led trigger time
    *((uint64_t*)buffer) = htonll(header.led_trigger_time);
    buffer += 8;

    // CT trigger time
    *((uint64_t*)buffer) = htonll(header.ct_time);
    buffer += 8;

    *((uint32_t*)buffer) = htonl(crc32(0, (uint32_t*)start, buffer-start));
    return 52;
}

static size_t produce_data(unsigned char* buffer, const int number, const int device_id, const int len) {
    const int NCHAN = 16;
    uint8_t i;
    unsigned char* start = buffer;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t timeticks = tv.tv_sec*1e6 + tv.tv_usec;

    (*(uint32_t*)buffer) = 0xFFFFFFFF;
    buffer += 4;
    (*(uint32_t*)buffer) = htonl(number);
    buffer += 4;

    (*(uint64_t*)buffer) = htonll(timeticks);
    // Skip the clock
    buffer += 8;

    // Length
    (*(uint16_t*)buffer) = htons(len);
    buffer += 2;

    // Device number
    (*(uint8_t*)buffer) = (uint8_t) device_id;
    buffer +=1;

    uint8_t crc = 0;
    for(i=0; i<(buffer - (start+4)); i++) {
        crc8(&crc, start[i+4]);
    }
    *buffer = (crc ^ 0x55);
    buffer +=1;

    for(i=0; i<NCHAN; i++) {
        buffer[0] = 0xFF;
        buffer[1] = i;
        buffer[2] = 0xFF;
        buffer[3] = i;
        buffer += 4;

        unsigned char* channel_start = buffer;
        for(int j=0; j<len; j++) {
            uint16_t val = 128 + ((rand() % 9) - 8);
            (*(uint16_t*)buffer) = val;
            buffer += 2;

            val = 128 + ((rand() % 9) - 8);
            (*(uint16_t*)buffer) = val;
            buffer += 2;

            *(uint32_t*)(buffer-4) = htonl(*((uint32_t*)(buffer-4)));
        }
        uint32_t channel_crc = crc32(0, (uint32_t*)channel_start, (buffer-channel_start));
        *((uint32_t*)buffer) = htonl(channel_crc);
        buffer += 4;

        // Now do delta-encoding
        buffer = channel_start;
        uint32_t word = ntohl(*((uint32_t*)buffer));
        int16_t prev = ((word>>16) & 0x3fff);
        uint16_t sample = (word & 0x3FFF);
        int16_t new_sample = sample - prev;

        (*(uint16_t*)buffer) = htons((prev & 0x3fff));
        buffer += 2;
        prev = sample;
        (*(uint16_t*)buffer) = htons((new_sample & 0x3fff));
        buffer += 2;


        for(int j=1; j<len; j++) {
            word = ntohl(*((uint32_t*)buffer));
            sample = ((word>>16) & 0x3FFF);
            new_sample = sample - prev;
            prev = sample;
            (*(int16_t*)buffer) = htons(new_sample & 0x3fff);
            buffer += 2;

            sample = (word & 0x3FFF);
            new_sample = sample - prev;
            (*(int16_t*)buffer) = htons(new_sample & 0x3fff);
            prev = sample;
            buffer += 2;
        }
        // Skip the CRC
        buffer += 4;
    }
    return buffer - start;
}

int main(int argc, char** argv) {
    int create_fontus_data = 0;
    if(argc > 1) {
        if(strcmp(argv[1], "--fontus") == 0) {
            printf("Will be producing FONTUS data for first connection\n");
            create_fontus_data = 1;
        }
        else {
            printf("Unrecognized argument. Only available argument is '--fontus'\n");
            return 0;
        }
    }

    sigignore(SIGPIPE);

    int sockfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    int yes=1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections on port %s...\n", PORT);

    int connected_fds[64];
    int num_connected_fds = 0;
    struct timeval event_rate_time, current_time, print_update_time;
    struct timeval timeout;
    event_rate_time.tv_sec = 0;
    event_rate_time.tv_usec = 0;
    print_update_time.tv_sec = 0;
    print_update_time.tv_usec = 0;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    double RATE = 1;

    double time_interval = 1e6/RATE;

    unsigned char* buffer = malloc((1024*1024));
    int count = 0;
    int sent_count = 0;
    //ssize_t nbytes = produce_data(buffer, count, 4, 400);

    while(1) {  // main accept() loop
        gettimeofday(&current_time, NULL);
        // Check if there's any new connections
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        if(select(sockfd+1, &readfds, NULL, NULL, &timeout) > 0) {
            sin_size = sizeof(their_addr);
            int connected_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
            if (connected_fd == -1) {
                perror("accept");
                continue;
            }
            connected_fds[num_connected_fds++] = connected_fd;
            if(num_connected_fds >= 64) {
                printf("TOO many connections\n");
                exit(1);
            }

            printf("server: got connection\n");
            continue;
        }

        double delta_t_send = (current_time.tv_sec - event_rate_time.tv_sec)*1e6 + (current_time.tv_usec - event_rate_time.tv_usec);
        double delta_t_print = (current_time.tv_sec - print_update_time.tv_sec)*1e6 + (current_time.tv_usec - print_update_time.tv_usec);

        if(delta_t_print > 1e6) {
            printf("Num Sent = %i\n", sent_count);
            print_update_time = current_time;
            sent_count = 0;
        }

        if(delta_t_send > time_interval && num_connected_fds > 0) {
            event_rate_time = current_time;
            // Send event
            for(int i =0; i<num_connected_fds; i++) {
                if(connected_fds[i] < 0) {
                    continue;
                }
                ssize_t nbytes;
                if(create_fontus_data && i==0) {
                    nbytes = produce_fontus_data(buffer, count);
                }
                else {
                    int channel_number = i+4;
                    channel_number -= create_fontus_data ? 1 : 0;
                    nbytes = produce_data(buffer, count, channel_number, 400);
                }

                ssize_t nsent = 0;

                do {
                    ssize_t bytes = send(connected_fds[i], buffer+nsent, nbytes-nsent, 0);
                    if(bytes <= 0) {
                        perror("SEND");
                        close(connected_fds[i]);
                        connected_fds[i] = -1;
                        break;
                    }
                    nsent +=  bytes;
                } while(nsent < nbytes);
            }
            sent_count += 1;
            count += 1;
        }
        else {
            usleep(1000);
        }
    }

    return 0;
}
