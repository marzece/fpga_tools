#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#define NCHANNELS 16
#define EVENT_BUFFER_BYTES (1024*1024) // 1MB SHOULD Be big enough for any single event

typedef struct TrigHeader{
    uint32_t magic_number;
    uint32_t trig_number;
    uint64_t clock;
    uint16_t length;
    uint8_t device_number;
    uint8_t crc;
} TrigHeader;

typedef struct ChannelHeader {
    uint8_t reserved1;
    uint8_t channel_id1;
    uint8_t reserved2;
    uint8_t channel_id2;
} ChannelHeader;

typedef struct EventIndex{
    int nevents;
    long* locations; // Array of offsets in file for event starts
    unsigned int* nsamples; // Array of lengths (number of samples) for events
} EventIndex;

int read_header(FILE* fin, TrigHeader* header);

int count_events(FILE* fin);

EventIndex get_events_index(FILE* fin, const unsigned int max_counts);

// Assumes that "samples" is of sufficienty length
// for nsamples*NCHANNELS number of samples
int read_event(FILE*fin, uint16_t nsamples, uint16_t *samples);

// Return time between two timestamps in seconds
double calc_delta_t(uint64_t this_time, uint64_t last_time);
