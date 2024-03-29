#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "data_parser.h"

uint16_t _bswap16(uint16_t a)
{
  a = ((a & 0x00FF) << 8) | ((a & 0xFF00) >> 8);
  return a;
}

uint32_t _bswap32(uint32_t a)
{
  a = ((a & 0x000000FF) << 24) |
      ((a & 0x0000FF00) <<  8) |
      ((a & 0x00FF0000) >>  8) |
      ((a & 0xFF000000) >> 24);
  return a;
}

uint64_t _bswap64(uint64_t a)
{
  a = ((a & 0x00000000000000FFULL) << 56) |
      ((a & 0x000000000000FF00ULL) << 40) |
      ((a & 0x0000000000FF0000ULL) << 24) |
      ((a & 0x00000000FF000000ULL) <<  8) |
      ((a & 0x000000FF00000000ULL) >>  8) |
      ((a & 0x0000FF0000000000ULL) >> 24) |
      ((a & 0x00FF000000000000ULL) >> 40) |
      ((a & 0xFF00000000000000ULL) >> 56);
  return a;
}

int  read_header(FILE* fin, TrigHeader* header) {
    if(!fread(&(header->magic_number), sizeof(uint32_t), 1, fin)) {
        return -1;
    }
    if(!fread(&(header->trig_number), sizeof(uint32_t), 1, fin)) {
        return -1;
    }
    if(!fread(&(header->clock), sizeof(uint64_t), 1, fin)) {
        return -1;
    }
    if(!fread(&(header->length), sizeof(uint16_t), 1, fin)) {
        return -1;
    }
    if(!fread(&(header->device_number), sizeof(uint8_t), 1, fin)) {
        return -1;
    }
    if(!fread(&(header->crc), sizeof(uint8_t), 1, fin)) {
        return -1;
    }
   header->magic_number = _bswap32(header->magic_number);
   header->trig_number = _bswap32(header->trig_number);
   header->clock = _bswap64(header->clock);
   header->length = ntohs(header->length);
   // header->magic_number = ntohl(header->magic_number);
   // header->trig_number = ntohl(header->trig_number);
   // header->clock = ntohll(header->clock);
   // header->length = ntohs(header->length);
    assert(header->magic_number == 0xFFFFFFFF);
    return 0;
}

int count_events(FILE* fin) {
    TrigHeader header;
    int count = 0;
    const int SAMPLE_BYTES = 2;
    const int CHANNEL_HEADER_BYTES = 4;
    const int CHANNEL_CRC_SIZE = 4;

    long initial_position = ftell(fin);

    //fseek(fin, 0, SEEK_END);
    //long file_size = ftell(fin);

    if(fseek(fin, 0, SEEK_SET)) {
        // TODO
         return -1;
    }

    while(read_header(fin, &header) == 0) {
        // Now need to determine how far to jump ahead to get next triggr header
        long bytes_forward  = NCHANNELS*(header.length*2*SAMPLE_BYTES + CHANNEL_HEADER_BYTES + CHANNEL_CRC_SIZE);
        fseek(fin, bytes_forward, SEEK_CUR);
        count += 1;
    }
    fseek(fin, initial_position, SEEK_SET);
    return count;
}

EventIndex get_events_index(FILE* fin, const unsigned int max_counts) {
    TrigHeader header;
    unsigned int count = 0;
    const int SAMPLE_BYTES = 2;
    const int CHANNEL_HEADER_BYTES = 4;
    const int CHANNEL_CRC_SIZE = 4;

    long initial_position = ftell(fin);

    //fseek(fin, 0, SEEK_END);
    //long file_size = ftell(fin);

    // First count how many events are in the file...that'll just make things easier cause I can
    // just allocated memory up front.
    // It might be more optimal not to do this b/c it means looping through the file twice.
    int nevents = count_events(fin);
    EventIndex index;
    index.locations = (long*)malloc(sizeof(long)*nevents);
    index.nsamples = (unsigned int*)malloc(sizeof(unsigned int)*nevents);
    index.nevents = 0;

    if(fseek(fin, 0, SEEK_SET)) {
        // TODO Add error message
        index.locations = NULL;
        index.nsamples = NULL;
        return index;
    }

    const int TRIGGER_HEADER_BYTES = 20;
    while(read_header(fin, &header) == 0 && (max_counts == 0  || count < max_counts)) {
        // Now need to determine how far to jump ahead to get next triggr header
        index.locations[count] = ftell(fin) - TRIGGER_HEADER_BYTES;
        index.nsamples[count] = header.length*2;

        long bytes_forward  = NCHANNELS*(header.length*2*SAMPLE_BYTES + CHANNEL_HEADER_BYTES + CHANNEL_CRC_SIZE);

        fseek(fin, bytes_forward, SEEK_CUR);
        count += 1;
    }
    fseek(fin, initial_position, SEEK_SET);
    index.nevents = count;
    return index;
}

int get_event(FILE* fin, long offset, uint16_t** samples, TrigHeader* _header) {
    TrigHeader header;
    long initial_position = ftell(fin);

    fseek(fin, offset, SEEK_SET);
    if(read_header(fin, &header) != 0) {
        // ERROR
        return -1;;
    }
    *samples = (uint16_t*)malloc(sizeof(uint16_t)*header.length*2*NCHANNELS);
    *_header = header;

    if(read_event(fin, header.length*2, *samples) != 0) {
        // ERROR
        free(*samples);
        return -1;;
    }
    fseek(fin, initial_position, SEEK_SET);

    return header.length*2;
}

uint16_t short_byte_swap(uint16_t in) {
    unsigned char* buf = (unsigned char*)&in;
    return (buf[1] | buf[0] << 8);
}

// Assumes that "samples" is of sufficienty length
// for nsamples*NCHANNELS number of samples
int read_event(FILE*fin, uint16_t nsamples, uint16_t *samples) {
    static int is_little_endian;
    static int first = 1;
    // Run time test of byte order, should only ever happen once.
    if(first) {
        is_little_endian = ((char*)&first)[0] != 0;
        first = 0;
    }
    int i, j;
    uint32_t crc;
    for(i=0; i<NCHANNELS; i++) {
        // First read the channel header 
        ChannelHeader chan_header;
        if(!fread(&(chan_header.reserved1), sizeof(uint8_t), 1, fin)) {
            return -1;
        }
        if(!fread(&(chan_header.channel_id1), sizeof(uint8_t), 1, fin)) {
            return -1;
        }
        if(!fread(&(chan_header.reserved2), sizeof(uint8_t), 1, fin)) {
            return -1;
        }
        if(!fread(&(chan_header.channel_id2), sizeof(uint8_t), 1, fin)) {
            return -1;
        }
        assert(chan_header.channel_id1 == chan_header.channel_id2);
        assert(chan_header.channel_id1 == i);
        assert(chan_header.reserved1 == chan_header.reserved2);
        assert(chan_header.reserved1 == 0xFF);

        if(fread(samples, sizeof(uint16_t), nsamples, fin) != nsamples) {
            return -1;
        }

        // Data is stored network byte order
        for(j=0; j<nsamples; j++) {
            //samples[j] = ntohs(samples[j]);
            samples[j] = _bswap16(samples[j]);
        }

        //Read channel trailer (CRC)
        if(!fread(&crc, sizeof(uint32_t), 1, fin)) {
            return -1;
        }
        samples += nsamples;
    }
    return 0;
}

// Return time between two timestamps in seconds
double calc_delta_t(uint64_t this_time, uint64_t last_time) {
    static double CLOCK_SPEED = 250e6; // 250 MHz;

    // Subtraction works over rollovers, don't need to worry about it.
    uint64_t diff = this_time - last_time;

    return diff/CLOCK_SPEED;
}
