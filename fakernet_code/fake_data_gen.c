#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>

#define PORT "5009"
#define BACKLOG 10


uint32_t crc32(uint32_t crc, uint32_t * buf, unsigned int len);
void crc8(unsigned char *crc, unsigned char m);

void sigchld_handler(int s) {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}
// get sockaddr, IPv4
void *get_in_addr(struct sockaddr *sa) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
}

size_t produce_data(unsigned char* buffer, int number, int device_id, int len) {
    const int NCHAN = 16;
    uint8_t i;
    unsigned char* start = buffer;

    (*(uint32_t*)buffer) = 0xFFFFFFFF;
    buffer += 4;
    (*(uint32_t*)buffer) = htonl(number);
    buffer += 4;

    //(*(uint64_t*)buffer) = htonll(number);
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
        uint32_t crc = crc32(0, (uint32_t*)channel_start, (buffer-channel_start));
        *((uint32_t*)buffer) = htonl(crc);
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
    (void)argc;
    (void)argv;


    int sockfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
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

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

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
    int sent_count;
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
                ssize_t nbytes = produce_data(buffer, count, i+4, 400);
                ssize_t nsent = 0;

                do {
                    ssize_t bytes = send(connected_fds[i], buffer+nsent, nbytes-nsent, 0);
                    if(bytes < 0) {
                        perror("SEND");
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
