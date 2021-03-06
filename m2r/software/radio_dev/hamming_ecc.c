#include "hamming_ecc.h"

static const uint8_t hamming_7_4_codewords[16] = {
    0x00, 0x0b, 0x17, 0x1c, 0x26, 0x2d, 0x31, 0x3a,
    0x45, 0x4e, 0x52, 0x59, 0x63, 0x68, 0x74, 0x7f
};

static const uint8_t hamming_7_4_datanibbles[128] = {
    0x0, 0x0, 0x0, 0x1, 0x0, 0x8, 0x4, 0x2,
    0x0, 0x1, 0x1, 0x1, 0x3, 0x5, 0x9, 0x1,
    0x0, 0x6, 0xa, 0x2, 0x3, 0x2, 0x2, 0x2,
    0x3, 0xb, 0x7, 0x1, 0x3, 0x3, 0x3, 0x2,
    0x0, 0x6, 0x4, 0xc, 0x4, 0x5, 0x4, 0x4,
    0xd, 0x5, 0x7, 0x1, 0x5, 0x5, 0x4, 0x5,
    0x6, 0x6, 0x7, 0x6, 0xe, 0x6, 0x4, 0x2,
    0x7, 0x6, 0x7, 0x7, 0x3, 0x5, 0x7, 0xf,
    0x0, 0x8, 0xa, 0xc, 0x8, 0x8, 0x9, 0x8,
    0xd, 0xb, 0x9, 0x1, 0x9, 0x8, 0x9, 0x9,
    0xa, 0xb, 0xa, 0xa, 0xe, 0x8, 0xa, 0x2,
    0xb, 0xb, 0xa, 0xb, 0x3, 0xb, 0x9, 0xf,
    0xd, 0xc, 0xc, 0xc, 0xe, 0x8, 0x4, 0xc,
    0xd, 0xd, 0xd, 0xc, 0xd, 0x5, 0x9, 0xf,
    0xe, 0x6, 0xa, 0xc, 0xe, 0xe, 0xe, 0xf,
    0xd, 0xb, 0x7, 0xf, 0xe, 0xf, 0xf, 0xf
};


/*
 * For each of 16 bytes in `data`, write two bytes to `coded`,
 * where the first coded byte is the codeword for the most significant nibble
 * of data byte, and the second coded byte is the codeword for the least
 * significant nibble. Sets the MSb of each byte in `coded` to 0.
 */
void data_to_coded(uint8_t* data, uint8_t* coded) {
    int i;
    for(i=0; i<16; i++) {
        coded[2*i    ] = hamming_7_4_codewords[data[i] >> 4  ];
        coded[2*i + 1] = hamming_7_4_codewords[data[i] & 0x0F];
    }
}

/*
 * For each two bytes in `coded`, extract one byte into `data`, correcting up
 * to one bit error per byte in `coded`. Ignores the MSb of each byte in
 * `coded`.
 */
void coded_to_data(uint8_t* coded, uint8_t* data) {
    int i;
    for(i=0; i<16; i++) {
        data[i]  = hamming_7_4_datanibbles[coded[2*i    ] & 0x7F] << 4;
        data[i] |= hamming_7_4_datanibbles[coded[2*i + 1] & 0x7F];
    }
}

/*
 * Interleave 32 bytes (all with MSb=0, i.e. 7bit) from `coded` into
 * 28 bytes in `interleaved`.
 * The first 4 bytes in `interleaved` contain bit 0 of each byte in `coded`,
 * with the very first byte in `interleaved` containing bit 0 (LSb) of the
 * first eight bytes from `coded`.
 * The next 4 bytes in `interleaved` contain bit 1 from each byte in `coded`,
 * again with the 5th byte containing bits from the first eight bytes in
 * `coded`, the 6th byte containing bits from the next eight bytes, etc.
 * NOTE: Endienness-sensitive, requires LE.
 */
void coded_to_interleaved(uint8_t* coded, uint8_t* interleaved) {
    int i, j;
    uint32_t* il = (uint32_t*)interleaved;
    for(i=0; i<7; i++) {
        il[i] = 0;
        for(j=0; j<32; j++) {
            il[i] |= ((coded[j] & (1<<i)) >> i) << j;
        }
    }
}

/*
 * Deinterleaves 28 bytes from `interleaved` into 32 bytes in `coded`.
 * See coded_to_interleaved for description.
 */
void interleaved_to_coded(uint8_t* interleaved, uint8_t* coded) {
    int i, j;
    uint32_t* il = (uint32_t*)interleaved;
    for(i=0; i<32; i++) {
        coded[i] = 0;
        for(j=0; j<7; j++) {
            coded[i] |= ((il[j] & (1<<i)) >> i) << j;
        }
    }
}
