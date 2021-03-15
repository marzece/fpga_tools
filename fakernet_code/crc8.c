/*
 * crc8.c
 *
 * Computes a 8-bit CRC
 *
 */

// Eric M
// Code stolen from www.rajivchakravorty.com/source-code/uncertainty/multimedia-sim/html/crc8_8c-source.html
// On March 11 2021


#define DI  0x07
#include <stdio.h>


static unsigned char crc8_table[256];     /* 8-bit table */

/*
 * Should be called before any other crc function.
 */
static void init_crc8()
{
  int i,j;
  unsigned char crc;

    for (i=0; i<256; i++) {
      crc = i;
      for (j=0; j<8; j++)
        crc = (crc << 1) ^ ((crc & 0x80) ? DI : 0);
      crc8_table[i] = crc & 0xFF;
    }
}

/*
 * For a byte array whose accumulated crc value is stored in *crc, computes
 * resultant crc obtained by appending m to the byte array */
void crc8(unsigned char *crc, unsigned char m)
{
    static int made_table=0;
    if (!made_table) {
        init_crc8();
        made_table = 1;
    }

    *crc = crc8_table[(*crc) ^ m];
    *crc &= 0xFF;
}
